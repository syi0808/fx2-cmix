#!/usr/bin/env python3

import argparse
import json
from pathlib import Path


def rate(bucket):
    return bucket["bits"] / bucket["bytes"] if bucket["bytes"] else 0.0


def print_summary(name, trace):
    print(f"{name}:")
    print(f"  predictor input: {trace['predictor_input_bytes']:,} bytes")
    print(
        f"  digits: {trace['digit_bytes']:,} "
        f"({trace['digit_ratio'] * 100:.4f}%)"
    )
    print(f"  overall: {trace['bits_per_byte']:.6f} bits/byte")
    print(f"  digits: {trace['bits_per_digit']:.6f} bits/digit")
    print(
        "  start / continue / after: "
        f"{trace['first_digit_bits_per_digit']:.6f} / "
        f"{trace['continuation_bits_per_digit']:.6f} / "
        f"{trace['after_digit_bits_per_byte']:.6f}"
    )


def print_dimension(title, baseline, variant, key):
    print(f"\n{title}")
    print(f"{'bucket':<16} {'baseline':>12} {'variant':>12} {'delta':>12}")
    for bucket_name, baseline_bucket in baseline[key].items():
        variant_bucket = variant[key][bucket_name]
        baseline_rate = rate(baseline_bucket)
        variant_rate = rate(variant_bucket)
        print(
            f"{bucket_name:<16} {baseline_rate:>12.6f} "
            f"{variant_rate:>12.6f} {variant_rate - baseline_rate:>+12.6f}"
        )


def compare(baseline_name, baseline, variant_name, variant):
    if baseline["predictor_input_bytes"] != variant["predictor_input_bytes"]:
        raise SystemExit("trace input byte counts differ")
    if baseline["digit_bytes"] != variant["digit_bytes"]:
        raise SystemExit("trace digit byte counts differ")

    delta = variant["bits_per_digit"] - baseline["bits_per_digit"]
    estimated_bytes = delta * baseline["digit_bytes"] / 8
    print(f"\n{variant_name} vs {baseline_name}:")
    print(f"  digit delta: {delta:+.6f} bits/digit")
    print(f"  modeled digit loss delta: {estimated_bytes:+,.0f} bytes")
    print(
        "  total modeled loss delta: "
        f"{(variant['predictor_input_bits'] - baseline['predictor_input_bits']) / 8:+,.0f} bytes"
    )
    for title, key in (
        ("Categories", "categories"),
        ("Position", "by_position"),
        ("Digit", "by_digit"),
        ("Left context", "by_left_context"),
    ):
        print_dimension(title, baseline, variant, key)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("traces", nargs="+", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    loaded = [(path.stem, json.loads(path.read_text())) for path in args.traces]
    if args.json:
        print(json.dumps(dict(loaded), indent=2))
        return

    for name, trace in loaded:
        print_summary(name, trace)
    if len(loaded) > 1:
        baseline_name, baseline = loaded[0]
        for variant_name, variant in loaded[1:]:
            compare(baseline_name, baseline, variant_name, variant)


if __name__ == "__main__":
    main()
