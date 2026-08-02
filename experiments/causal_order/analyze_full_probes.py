#!/usr/bin/env python3

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Compare proxy and full edge ranks.")
    parser.add_argument("--index", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    return parser.parse_args()


def rank(values):
    order = sorted(range(len(values)), key=values.__getitem__)
    result = [0.0] * len(values)
    for position, index in enumerate(order):
        result[index] = position
    return result


def correlation(left, right):
    if len(left) < 2:
        return 0.0
    left = rank(left)
    right = rank(right)
    left_mean = sum(left) / len(left)
    right_mean = sum(right) / len(right)
    numerator = sum(
        (a - left_mean) * (b - right_mean) for a, b in zip(left, right)
    )
    denominator = math.sqrt(
        sum((a - left_mean) ** 2 for a in left)
        * sum((b - right_mean) ** 2 for b in right)
    )
    return numerator / denominator if denominator else 0.0


def trace_bits(path: Path) -> float:
    with path.open("r", encoding="utf-8") as source:
        for row in csv.DictReader(source, delimiter="\t"):
            if row["kind"] == "article":
                return float(row["ideal_bits"])
    raise ValueError(f"no article row in {path}")


def main() -> int:
    args = parse_args()
    groups = defaultdict(dict)
    with args.index.open("r", encoding="utf-8") as source:
        for row in csv.DictReader(source, delimiter="\t"):
            bits = trace_bits(Path(row["trace"]))
            groups[int(row["target_id"])][row["kind"]] = (
                float(row["proxy_bits"]), bits
            )
    proxy_ranks = []
    full_ranks = []
    for values in groups.values():
        kinds = sorted(values)
        proxy_ranks.extend(rank([values[kind][0] for kind in kinds]))
        full_ranks.extend(rank([values[kind][1] for kind in kinds]))
    optimized_wins = 0
    optimized_losses = 0
    random_wins = 0
    for values in groups.values():
        if values["optimized"][1] < values["baseline"][1]:
            optimized_wins += 1
        elif values["optimized"][1] > values["baseline"][1]:
            optimized_losses += 1
        if values["random"][1] < values["baseline"][1]:
            random_wins += 1
    report = {
        "targets": len(groups),
        "edges": len(proxy_ranks),
        "spearman_proxy_full_within_target": correlation(
            proxy_ranks, full_ranks
        ),
        "optimized_predecessor_wins": optimized_wins,
        "optimized_predecessor_losses": optimized_losses,
        "random_predecessor_wins": random_wins,
    }
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
