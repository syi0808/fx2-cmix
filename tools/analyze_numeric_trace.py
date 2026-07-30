#!/usr/bin/env python3

import argparse
import json
import math
import struct
from pathlib import Path


ALPHAS = (0, 0.03125, 0.0625, 0.125, 0.25, 0.5, 1.0)
PROBABILITY_RECORD = struct.Struct("<QHHBBBB")
TERMINATOR_NAMES = {
    0: "continued",
    1: "space",
    2: "markup",
    3: "dash",
    4: "colon",
    5: "dot",
    6: "comma",
    7: "slash",
    8: "other",
}


def rate(bucket):
    return bucket["bits"] / bucket["events"] if bucket["events"] else 0.0


def compare_bucket(name, baseline, variant):
    baseline_rate = rate(baseline)
    variant_rate = rate(variant)
    delta_bits = variant["bits"] - baseline["bits"]
    return (
        name,
        baseline["events"],
        baseline_rate,
        variant_rate,
        variant_rate - baseline_rate,
        delta_bits,
    )


def print_rows(title, rows):
    print(f"\n{title}")
    print(
        f"{'bucket':<24} {'events':>12} {'baseline':>12} "
        f"{'variant':>12} {'delta/event':>12} {'total bits':>12}"
    )
    for row in sorted(rows, key=lambda item: abs(item[5]), reverse=True):
        print(
            f"{row[0]:<24} {row[1]:>12,} {row[2]:>12.6f} "
            f"{row[3]:>12.6f} {row[4]:>+12.6f} {row[5]:>+12.3f}"
        )


def validate(baseline, variant):
    for key, bucket in baseline["events"].items():
        if bucket["events"] != variant["events"][key]["events"]:
            raise SystemExit(f"event count differs for {key}")


def compare(baseline_name, baseline, variant_name, variant):
    validate(baseline, variant)
    print(f"\n{variant_name} vs {baseline_name}")
    all_delta = (
        variant["events"]["all"]["bits"] -
        baseline["events"]["all"]["bits"]
    )
    print(f"  total ideal loss delta: {all_delta:+,.3f} bits")
    print(f"  total ideal byte delta: {all_delta / 8:+,.3f} bytes")
    event_rows = [
        compare_bucket(key, bucket, variant["events"][key])
        for key, bucket in baseline["events"].items()
    ]
    print_rows("Events", event_rows)
    print_residual_comparison(baseline, variant)
    for dimension in (
        "terminator",
        "separator_type",
        "previous_run_length",
        "segment_index",
        "shape_state",
        "start_structural_class",
        "start_word_bucket",
        "start_wrt_bucket",
    ):
        rows = []
        for key, bucket in baseline.get(dimension, {}).items():
            if key in variant.get(dimension, {}):
                rows.append(compare_bucket(
                    key, bucket, variant[dimension][key]))
        print_rows(dimension.replace("_", " ").title(), rows)


def print_residual_rows(title, baseline_buckets, variant_buckets, names):
    print(f"\n{title}")
    print(
        f"{'bucket':<20} {'events':>10} {'B0 final':>12} "
        f"{'standalone':>12} {'variant final':>14} {'final delta':>12}"
    )
    for name in names:
        baseline = baseline_buckets[name]
        variant = variant_buckets[name]
        count = baseline["events"]
        baseline_rate = rate(baseline)
        standalone = (
            variant["standalone_bits"]["boundary"] / count if count else 0
        )
        variant_rate = rate(variant)
        print(
            f"{name:<20} {count:>10,} {baseline_rate:>12.6f} "
            f"{standalone:>12.6f} {variant_rate:>14.6f} "
            f"{variant_rate - baseline_rate:>+12.6f}"
        )


def print_residual_comparison(baseline, variant):
    print_residual_rows(
        "Final vs standalone boundary",
        baseline["events"],
        variant["events"],
        ("boundary_active", "run_continued", "run_ended"),
    )
    common_terminators = sorted(
        set(baseline.get("terminator", {})) &
        set(variant.get("terminator", {}))
    )
    print_residual_rows(
        "Terminator residual comparison",
        baseline.get("terminator", {}),
        variant.get("terminator", {}),
        common_terminators,
    )
    baseline_inactive = baseline["events"]["boundary_inactive"]
    variant_inactive = variant["events"]["boundary_inactive"]
    print(
        "\nInactive final loss delta: "
        f"{variant_inactive['bits'] - baseline_inactive['bits']:+.6f} bits "
        f"over {baseline_inactive['events']:,} bytes"
    )


def probability(value):
    return max(1 / 65536, min(65535 / 65536, value / 65536))


def bit_loss(actual, predicted):
    return -math.log2(predicted if actual else 1 - predicted)


def logit(value):
    return math.log(value / (1 - value))


def logistic(value):
    if value >= 0:
        exponent = math.exp(-value)
        return 1 / (1 + exponent)
    exponent = math.exp(value)
    return exponent / (1 + exponent)


def read_probability_trace(path, warmup_bytes):
    records = []
    with path.open("rb") as input_file:
        if input_file.read(8) != b"FX2NBP1\0":
            raise SystemExit(f"invalid probability trace: {path}")
        while raw := input_file.read(PROBABILITY_RECORD.size):
            if len(raw) != PROBABILITY_RECORD.size:
                raise SystemExit(f"truncated probability trace: {path}")
            record = PROBABILITY_RECORD.unpack(raw)
            if record[0] >= warmup_bytes:
                records.append(record)
    return records


def alpha_sweep(baseline_path, variant_path, warmup_bytes):
    baseline = read_probability_trace(baseline_path, warmup_bytes)
    variant = read_probability_trace(variant_path, warmup_bytes)
    if len(baseline) != len(variant):
        raise SystemExit("probability trace record counts differ")

    categories = {"boundary_active": []}
    for baseline_record, variant_record in zip(baseline, variant):
        baseline_key = (
            baseline_record[0],
            baseline_record[3],
            baseline_record[4],
            baseline_record[5],
            baseline_record[6],
        )
        variant_key = (
            variant_record[0],
            variant_record[3],
            variant_record[4],
            variant_record[5],
            variant_record[6],
        )
        if baseline_key != variant_key:
            raise SystemExit(
                f"probability traces diverge at byte {baseline_record[0]}"
            )
        event_name = "continued" if baseline_record[5] == 1 else "ended"
        terminator_name = TERMINATOR_NAMES[baseline_record[6]]
        sample = (
            baseline_record[4],
            probability(baseline_record[1]),
            probability(variant_record[2]),
            probability(variant_record[1]),
        )
        categories["boundary_active"].append(sample)
        categories.setdefault(event_name, []).append(sample)
        if event_name == "ended":
            categories.setdefault(terminator_name, []).append(sample)

    print(
        f"\nOffline logit blend after {warmup_bytes:,} warm-up bytes "
        f"({len(baseline):,} active bits)"
    )
    print(
        f"{'category':<18} {'alpha':>8} {'bits':>14} "
        f"{'delta vs B0':>14} {'variant final':>15}"
    )
    for category_name, samples in categories.items():
        baseline_bits = sum(
            bit_loss(actual, baseline_probability)
            for actual, baseline_probability, _, _ in samples
        )
        variant_bits = sum(
            bit_loss(actual, variant_probability)
            for actual, _, _, variant_probability in samples
        )
        for alpha in ALPHAS:
            blended_bits = 0
            for actual, baseline_probability, boundary_probability, _ in samples:
                blended = logistic(
                    logit(baseline_probability) +
                    alpha * logit(boundary_probability)
                )
                blended_bits += bit_loss(actual, blended)
            print(
                f"{category_name:<18} {alpha:>8.5f} {blended_bits:>14.6f} "
                f"{blended_bits - baseline_bits:>+14.6f} "
                f"{variant_bits:>15.6f}"
            )


def print_summary(name, trace):
    events = trace["events"]
    print(f"{name}:")
    print(f"  predictor bytes: {events['all']['events']:,}")
    print(f"  numeric starts: {events['start_digit']['events']:,}")
    print(f"  run boundaries: {events['run_ended']['events']:,}")
    print(f"  separators: {events['separator']['events']:,}")
    print(f"  linked starts: {events['linked_run_start']['events']:,}")
    print(f"  ideal bits: {events['all']['bits']:,.3f}")
    print("  standalone target loss:")
    for event_name, model_name in (
        ("run_continued", "boundary"),
        ("run_ended", "boundary"),
        ("linked_run_start", "linked"),
        ("start_digit", "start_coarse"),
    ):
        bucket = events[event_name]
        standalone = bucket["standalone_bits"][model_name]
        standalone_rate = standalone / bucket["events"] if bucket["events"] else 0
        print(
            f"    {model_name}/{event_name}: "
            f"{standalone_rate:.6f} bits/event"
        )
    focused = []
    for dimension in (
        "terminator",
        "separator_type",
        "previous_run_length",
        "segment_index",
        "shape_state",
        "start_structural_class",
        "start_word_bucket",
        "start_wrt_bucket",
    ):
        for bucket_name, bucket in trace.get(dimension, {}).items():
            focused.append((
                bucket["bits"],
                dimension,
                bucket_name,
                bucket["events"],
                rate(bucket),
            ))
    print("  highest structural loss:")
    for bits, dimension, bucket_name, events_count, bits_per_event in sorted(
            focused, reverse=True)[:8]:
        print(
            f"    {dimension}/{bucket_name}: {bits:,.3f} bits "
            f"({events_count:,} events, {bits_per_event:.6f}/event)"
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("traces", nargs="+", type=Path)
    parser.add_argument(
        "--probability-traces", nargs=2, type=Path,
        metavar=("BASELINE", "VARIANT"),
    )
    parser.add_argument("--warmup-bytes", type=int, default=0)
    args = parser.parse_args()
    loaded = [(path.stem, json.loads(path.read_text()))
              for path in args.traces]
    for name, trace in loaded:
        print_summary(name, trace)
    if len(loaded) > 1:
        baseline_name, baseline = loaded[0]
        for variant_name, variant in loaded[1:]:
            compare(baseline_name, baseline, variant_name, variant)
    if args.probability_traces:
        alpha_sweep(
            args.probability_traces[0],
            args.probability_traces[1],
            args.warmup_bytes,
        )


if __name__ == "__main__":
    main()
