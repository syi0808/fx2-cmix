#!/usr/bin/env python3

import argparse
import random
import re
from pathlib import Path


PAGE_START = b"  <page>\n"
PAGE_END = b"  </page>\n"
TITLE_PATTERN = re.compile(br"<title>(.*?)</title>", re.DOTALL)
TEXT_START_PATTERN = re.compile(br"<text(?:\s[^>]*)?>")


def parse_segment(value: str) -> tuple[int, int]:
    try:
        start_text, count_text = value.split(":", 1)
        start, count = int(start_text), int(count_text)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "segment must have the form START:COUNT"
        ) from error
    if start < 0 or count <= 0:
        raise argparse.ArgumentTypeError("segment values must be non-negative")
    return start, count


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build a bounded article-order corpus from enwik9."
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--features", required=True, type=Path)
    parser.add_argument("--order", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--segment",
        action="append",
        type=parse_segment,
        help="ordered-rank range START:COUNT; may be repeated",
    )
    parser.add_argument(
        "--text-bytes",
        type=int,
        default=1024,
        help="maximum source text bytes retained per page",
    )
    parser.add_argument(
        "--shuffle-seed",
        type=int,
        help="deterministically shuffle the selected article order",
    )
    parser.add_argument(
        "--preserve-pages",
        action="store_true",
        help="copy complete source pages instead of truncating their text",
    )
    args = parser.parse_args()
    if args.text_bytes <= 0:
        parser.error("--text-bytes must be positive")
    if not args.segment:
        args.segment = [(0, 4096)]
    return args


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
            fields = line.rstrip("\n").split("\t")
            mapping[int(fields[nonredirect_column])] = int(fields[article_column])
    return mapping


def load_order(path: Path, mapping: dict[int, int]) -> list[int]:
    article_ids = []
    with path.open("r", encoding="utf-8") as source:
        for line in source:
            nonredirect_id = int(line)
            try:
                article_ids.append(mapping[nonredirect_id])
            except KeyError as error:
                raise ValueError(
                    f"unknown non-redirect article id {nonredirect_id}"
                ) from error
    if len(set(article_ids)) != len(article_ids):
        raise ValueError("article order contains duplicates")
    return article_ids


def select_articles(
    ordered_ids: list[int], segments: list[tuple[int, int]]
) -> list[int]:
    selected = []
    selected_ranks = set()
    for start, count in segments:
        end = start + count
        if end > len(ordered_ids):
            raise ValueError(
                f"segment {start}:{count} exceeds {len(ordered_ids)} ordered articles"
            )
        for rank in range(start, end):
            if rank in selected_ranks:
                raise ValueError(f"overlapping segment at ordered rank {rank}")
            selected_ranks.add(rank)
            selected.append(ordered_ids[rank])
    return selected


def extract_page(
    source, offset: tuple[int, int], text_bytes: int, preserve_page: bool
) -> bytes:
    start, end = offset
    source.seek(start)
    page = source.read(end - start)
    if preserve_page:
        return page
    title_match = TITLE_PATTERN.search(page)
    text_start_match = TEXT_START_PATTERN.search(page)
    if title_match is None or text_start_match is None:
        raise ValueError(f"page at byte {start} lacks a title or text element")
    text_start = text_start_match.end()
    text_end = page.find(b"</text>", text_start)
    if text_end < 0:
        raise ValueError(f"page at byte {start} lacks a closing text element")
    text = page[text_start:min(text_end, text_start + text_bytes)]
    return (
        PAGE_START
        + b"    <title>"
        + title_match.group(1)
        + b"</title>\n"
        + b"    <revision>\n"
        + b'      <text xml:space="preserve">'
        + text
        + b"</text>\n"
        + b"    </revision>\n"
        + PAGE_END
    )


def main() -> int:
    args = parse_args()
    offsets = load_page_offsets(args.input)
    mapping = load_article_map(args.features)
    ordered_ids = load_order(args.order, mapping)
    selected_ids = select_articles(ordered_ids, args.segment)
    if args.shuffle_seed is not None:
        random.Random(args.shuffle_seed).shuffle(selected_ids)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.input.open("rb") as source, args.output.open("wb") as output:
        output.write(b"<mediawiki>\n")
        for article_id in selected_ids:
            output.write(
                extract_page(
                    source,
                    offsets[article_id],
                    args.text_bytes,
                    args.preserve_pages,
                )
            )
        output.write(b"</mediawiki>\n")

    print(
        f"wrote {len(selected_ids)} articles and {args.output.stat().st_size} bytes "
        f"to {args.output}"
        + (
            f" (shuffle seed {args.shuffle_seed})"
            if args.shuffle_seed is not None
            else ""
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
