#!/usr/bin/env python3

import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path


BASELINE_SCALE = 2


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compile held-out final-probability control rules."
    )
    parser.add_argument("trace", type=Path)
    parser.add_argument("--output-include", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--max-rules", type=int, default=16)
    parser.add_argument("--minimum-support", type=int, default=65536)
    parser.add_argument("--rule-cost-bits", type=float, default=40.0)
    return parser.parse_args()


def load_trace(path: Path):
    cells = defaultdict(lambda: defaultdict(lambda: [0, 0.0]))
    article_bits = 0
    with path.open("r", encoding="utf-8") as source:
        for row in csv.DictReader(source, delimiter="\t"):
            if row["kind"] == "article":
                article_bits += int(row["count"])
                continue
            split = int(row["split"])
            cell = (int(row["confidence"]), int(row["disagreement"]))
            scale = int(row["scale_class"])
            cells[(split, cell)][scale][0] += int(row["count"])
            cells[(split, cell)][scale][1] += float(row["ideal_bits"])
    return cells, article_bits


def region(cells, splits, cell, scale):
    count = 0
    bits = 0.0
    for split in splits:
        values = cells[(split, cell)].get(scale)
        if values:
            count += values[0]
            bits += values[1]
    return count, bits


def gain(cells, splits, cell, scale):
    count, baseline = region(cells, splits, cell, BASELINE_SCALE)
    _, candidate = region(cells, splits, cell, scale)
    return count, baseline - candidate


def compile_rules(cells, args):
    candidates = []
    all_cells = sorted({key[1] for key in cells})
    for cell in all_cells:
        support, _ = gain(cells, range(5), cell, BASELINE_SCALE)
        if support < args.minimum_support:
            continue
        best = max(
            (gain(cells, range(5), cell, scale)[1], scale)
            for scale in range(5)
            if scale != BASELINE_SCALE
        )
        training_gain, scale = best
        _, tuning_gain = gain(cells, [5], cell, scale)
        _, validation_gain = gain(cells, [6], cell, scale)
        if training_gain <= args.rule_cost_bits:
            continue
        if tuning_gain <= 0 or validation_gain <= 0:
            continue
        candidates.append(
            {
                "confidence": cell[0],
                "disagreement": cell[1],
                "scale_class": scale,
                "support": support,
                "training_gain_bits": training_gain,
                "tuning_gain_bits": tuning_gain,
                "validation_gain_bits": validation_gain,
            }
        )
    candidates.sort(key=lambda rule: rule["validation_gain_bits"], reverse=True)
    return candidates[: args.max_rules]


def split_gain(cells, rules, split):
    total = 0.0
    for rule in rules:
        cell = (rule["confidence"], rule["disagreement"])
        total += gain(cells, [split], cell, rule["scale_class"])[1]
    return total


def write_include(path: Path, rules):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as output:
        output.write("static constexpr ControlRule kControlRules[] = {\n")
        for rule in rules:
            confidence = rule["confidence"]
            disagreement = rule["disagreement"]
            output.write(
                f"    {{{confidence}, {confidence}, {disagreement}, "
                f"{disagreement}, {rule['scale_class']}}},\n"
            )
        output.write("    {255, 255, 255, 255, 2},\n};\n")


def main() -> int:
    args = parse_args()
    cells, article_bits = load_trace(args.trace)
    rules = compile_rules(cells, args)
    write_include(args.output_include, rules)
    split_gains = {str(split): split_gain(cells, rules, split) for split in range(8)}
    report = {
        "article_bits": article_bits,
        "rule_count": len(rules),
        "rules": rules,
        "split_gain_bits": split_gains,
        "training_gain_bits": sum(split_gains[str(split)] for split in range(5)),
        "tuning_gain_bits": split_gains["5"],
        "validation_gain_bits": split_gains["6"],
        "holdout_gain_bits": split_gains["7"],
        "all_gain_bits": sum(split_gains.values()),
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
