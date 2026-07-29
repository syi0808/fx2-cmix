#!/usr/bin/env python3

import argparse
import math
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Encode an existing article order as smooth position features."
    )
    parser.add_argument("--features", required=True, type=Path)
    parser.add_argument("--order", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--bins", type=int, default=64)
    parser.add_argument("--width", type=float, default=1.5)
    return parser.parse_args()


def load_article_map(path: Path) -> tuple[dict[int, int], list[int]]:
    nonredirect_to_article = {}
    article_ids = []
    with path.open("r", encoding="utf-8") as source:
        header = source.readline().rstrip("\n").split("\t")
        article_column = header.index("article_index")
        nonredirect_column = header.index("nonredirect_index")
        for line in source:
            fields = line.split("\t", max(article_column, nonredirect_column) + 1)
            article_id = int(fields[article_column])
            nonredirect_id = int(fields[nonredirect_column])
            nonredirect_to_article[nonredirect_id] = article_id
            article_ids.append(article_id)
    return nonredirect_to_article, article_ids


def main() -> int:
    args = parse_args()
    if args.bins < 2:
        raise ValueError("--bins must be at least 2")
    if args.width <= 0:
        raise ValueError("--width must be positive")

    nonredirect_to_article, article_ids = load_article_map(args.features)
    ordered_ids = []
    with args.order.open("r", encoding="utf-8") as source:
        for line in source:
            nonredirect_id = int(line)
            if nonredirect_id not in nonredirect_to_article:
                raise ValueError(f"unknown non-redirect article id {nonredirect_id}")
            ordered_ids.append(nonredirect_to_article[nonredirect_id])

    used = set(ordered_ids)
    if len(used) != len(ordered_ids):
        raise ValueError("article order contains duplicates")
    ordered_ids.extend(article_id for article_id in article_ids if article_id not in used)
    scale = max(1, len(ordered_ids) - 1)
    sigma = args.width / (args.bins - 1)
    centers = [index / (args.bins - 1) for index in range(args.bins)]

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as output:
        output.write(
            "\t".join(
                ["article_index", *(f"position_{index:03d}" for index in range(args.bins))]
            )
        )
        output.write("\n")
        for rank, article_id in enumerate(ordered_ids):
            position = rank / scale
            values = [
                math.exp(-0.5 * ((position - center) / sigma) ** 2)
                for center in centers
            ]
            output.write(
                "\t".join(
                    [str(article_id), *(format(value, ".9g") for value in values)]
                )
            )
            output.write("\n")
    print(f"wrote {len(ordered_ids)} position features")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
