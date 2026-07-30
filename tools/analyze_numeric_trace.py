#!/usr/bin/env python3

import argparse
import json
from pathlib import Path


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
    args = parser.parse_args()
    loaded = [(path.stem, json.loads(path.read_text()))
              for path in args.traces]
    for name, trace in loaded:
        print_summary(name, trace)
    if len(loaded) > 1:
        baseline_name, baseline = loaded[0]
        for variant_name, variant in loaded[1:]:
            compare(baseline_name, baseline, variant_name, variant)


if __name__ == "__main__":
    main()
