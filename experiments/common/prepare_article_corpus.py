#!/usr/bin/env python3

import argparse
import hashlib
from dataclasses import dataclass
from pathlib import Path


PAGE_START = b"  <page>\n"
PAGE_END = b"  </page>\n"


@dataclass(frozen=True)
class Article:
    article_index: int
    nonredirect_index: int
    source_begin: int
    source_end: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build a fixed-size article corpus in an existing order."
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--features", required=True, type=Path)
    parser.add_argument("--order", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--selected-order", required=True, type=Path)
    parser.add_argument("--target-bytes", type=int, default=20 * 1024 * 1024)
    return parser.parse_args()


def load_offsets(path: Path) -> list[tuple[int, int]]:
    offsets = []
    begin = None
    with path.open("rb") as source:
        while line := source.readline():
            if line == PAGE_START:
                begin = source.tell() - len(line)
            elif line == PAGE_END and begin is not None:
                offsets.append((begin, source.tell()))
                begin = None
    return offsets


def load_articles(path: Path, offsets: list[tuple[int, int]]) -> dict[int, Article]:
    articles = {}
    with path.open("r", encoding="utf-8") as source:
        header = source.readline().rstrip("\n").split("\t")
        article_column = header.index("article_index")
        nonredirect_column = header.index("nonredirect_index")
        for line in source:
            fields = line.rstrip("\n").split("\t")
            article_index = int(fields[article_column])
            nonredirect_index = int(fields[nonredirect_column])
            begin, end = offsets[article_index]
            articles[nonredirect_index] = Article(
                article_index, nonredirect_index, begin, end
            )
    return articles


def load_order(path: Path, articles: dict[int, Article]) -> list[Article]:
    selected = []
    seen = set()
    with path.open("r", encoding="utf-8") as source:
        for line in source:
            nonredirect_index = int(line)
            article = articles.get(nonredirect_index)
            if article is None:
                continue
            if nonredirect_index in seen:
                raise ValueError(f"duplicate article {nonredirect_index}")
            seen.add(nonredirect_index)
            selected.append(article)
    selected.extend(
        sorted(
            (article for key, article in articles.items() if key not in seen),
            key=lambda article: article.article_index,
        )
    )
    return selected


def main() -> int:
    args = parse_args()
    offsets = load_offsets(args.input)
    articles = load_articles(args.features, offsets)
    ordered = load_order(args.order, articles)
    wrapper_bytes = len(b"<mediawiki>\n") + len(b"</mediawiki>\n")
    selected = []
    total = wrapper_bytes
    for article in ordered:
        article_bytes = article.source_end - article.source_begin
        if selected and total + article_bytes > args.target_bytes:
            break
        selected.append(article)
        total += article_bytes
    if not selected:
        raise ValueError("target size is smaller than the first article")

    for path in (args.output, args.manifest, args.selected_order):
        path.parent.mkdir(parents=True, exist_ok=True)
    records = []
    with args.input.open("rb") as source, args.output.open("wb") as output:
        output.write(b"<mediawiki>\n")
        for rank, article in enumerate(selected):
            begin = output.tell()
            source.seek(article.source_begin)
            output.write(source.read(article.source_end - article.source_begin))
            end = output.tell()
            split = int.from_bytes(
                hashlib.blake2s(
                    str(article.nonredirect_index).encode(), digest_size=4
                ).digest(),
                "little",
            ) % 8
            records.append((rank, article, begin, end, split))
        output.write(b"</mediawiki>\n")

    with args.manifest.open("w", encoding="utf-8") as output:
        output.write(
            "rank\tarticle_id\tsource_article\tstream_begin\tstream_end\tsplit\n"
        )
        for rank, article, begin, end, split in records:
            output.write(
                f"{rank}\t{article.nonredirect_index}\t{article.article_index}"
                f"\t{begin}\t{end}\t{split}\n"
            )
    with args.selected_order.open("w", encoding="utf-8") as output:
        for article in selected:
            output.write(f"{article.nonredirect_index}\n")

    print(
        f"wrote {len(selected)} articles and {args.output.stat().st_size} bytes "
        f"to {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
