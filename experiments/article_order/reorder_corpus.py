#!/usr/bin/env python3

import argparse
import sys
from pathlib import Path


PAGE_START = b"  <page>\n"
PAGE_END = b"  </page>\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Stream enwik9 pages in a redirect-skipping order."
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--features", required=True, type=Path)
    parser.add_argument("--order", required=True, type=Path)
    return parser.parse_args()


def load_page_offsets(path: Path) -> list[tuple[int, int]]:
    offsets = []
    start = None
    with path.open("rb") as source:
        while line := source.readline():
            if line == PAGE_START:
                start = source.tell() - len(line)
            elif line == PAGE_END and start is not None:
                offsets.append((start, source.tell()))
                start = None
    return offsets


def load_article_map(path: Path) -> dict[int, int]:
    mapping = {}
    with path.open("r", encoding="utf-8") as source:
        header = source.readline().rstrip("\n").split("\t")
        article_column = header.index("article_index")
        nonredirect_column = header.index("nonredirect_index")
        for line in source:
            fields = line.split("\t", max(article_column, nonredirect_column) + 1)
            mapping[int(fields[nonredirect_column])] = int(fields[article_column])
    return mapping


def main() -> int:
    args = parse_args()
    offsets = load_page_offsets(args.input)
    nonredirect_to_article = load_article_map(args.features)
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
    ordered_ids.extend(article_id for article_id in range(len(offsets)) if article_id not in used)

    output = sys.stdout.buffer
    with args.input.open("rb") as source:
        for article_id in ordered_ids:
            start, end = offsets[article_id]
            source.seek(start)
            remaining = end - start
            while remaining:
                chunk = source.read(min(1024 * 1024, remaining))
                if not chunk:
                    raise EOFError(f"unexpected EOF in article {article_id}")
                output.write(chunk)
                remaining -= len(chunk)
    print(f"wrote {len(ordered_ids)} complete articles", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
