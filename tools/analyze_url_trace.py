#!/usr/bin/env python3

import argparse
import csv
from pathlib import Path


def load_trace(path):
    rows = {}
    with path.open(newline="") as source:
        for row in csv.DictReader(source):
            rows[(row["dimension"], row["key"])] = {
                "urls": int(row["url_count"]),
                "bytes": int(row["bytes"]),
                "bits": float(row["bits"]),
            }
    return rows


def summarize(baseline, variant, dimension, limit):
    keys = {
        key for current_dimension, key in baseline
        if current_dimension == dimension
    } | {
        key for current_dimension, key in variant
        if current_dimension == dimension
    }
    results = []
    for key in keys:
        before = baseline.get((dimension, key), {})
        after = variant.get((dimension, key), {})
        byte_count = min(before.get("bytes", 0), after.get("bytes", 0))
        if not byte_count:
            continue
        before_bits = before.get("bits", 0)
        after_bits = after.get("bits", 0)
        results.append((
            before_bits - after_bits,
            key,
            byte_count,
            before_bits / byte_count,
            after_bits / byte_count,
            after.get("urls", 0),
        ))
    results.sort(reverse=True)
    print(f"\n{dimension}")
    print("key\tcount\tbytes\tbpb_before\tbpb_after\tdelta_bits")
    for gain, key, byte_count, before_bpb, after_bpb, count in results[:limit]:
        print(
            f"{key}\t{count}\t{byte_count}\t{before_bpb:.6f}\t"
            f"{after_bpb:.6f}\t{gain:.3f}"
        )


def main():
    parser = argparse.ArgumentParser(
        description="Compare baseline and URL-model surprisal traces."
    )
    parser.add_argument("baseline", type=Path)
    parser.add_argument("variant", type=Path)
    parser.add_argument("--limit", type=int, default=30)
    args = parser.parse_args()

    baseline = load_trace(args.baseline)
    variant = load_trace(args.variant)
    for dimension in ("total", "region", "role", "domain", "endpoint"):
        summarize(baseline, variant, dimension, args.limit)


if __name__ == "__main__":
    main()
