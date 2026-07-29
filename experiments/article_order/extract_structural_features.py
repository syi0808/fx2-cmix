#!/usr/bin/env python3

import argparse
import math
import re
import sys
from pathlib import Path

import numpy as np


PAGE_START = b"  <page>\n"
PAGE_END = b"  </page>\n"
REDIRECT_PREFIXES = (
    b'      <text xml:space="preserve">#REDIRECT',
    b'      <text xml:space="preserve">#redirect',
    b'      <text xml:space="preserve">#Redirect',
    b'      <text xml:space="preserve">#REdirect',
    b'      <text xml:space="preserve">{{softredirect',
)
TOKEN_PATTERNS = (
    ("template_open", b"{{"),
    ("template_close", b"}}"),
    ("link_open", b"[["),
    ("link_close", b"]]"),
    ("table_open", b"{|"),
    ("table_row", b"|-"),
    ("math_tag", b"<math"),
    ("ref_tag", b"<ref"),
    ("html_tag", b"<"),
    ("equals", b"="),
    ("pipe", b"|"),
)
WORD_PATTERN = re.compile(rb"[A-Za-z]+")
NUMBER_PATTERN = re.compile(rb"\b[0-9]+\b")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract compression-oriented structural features per enwik9 article."
    )
    parser.add_argument("input", type=Path, help="enwik9-compatible XML input")
    parser.add_argument("output", type=Path, help="output TSV")
    parser.add_argument(
        "--include-redirects",
        action="store_true",
        help="emit redirect articles as well",
    )
    parser.add_argument(
        "--progress-every",
        type=int,
        default=10000,
        help="report progress after this many articles; 0 disables it",
    )
    return parser.parse_args()


def safe_ratio(numerator: float, denominator: float) -> float:
    return numerator / denominator if denominator else 0.0


def article_features(data: bytes) -> list[float]:
    byte_values = np.frombuffer(data, dtype=np.uint8)
    histogram = np.bincount(byte_values, minlength=256).astype(np.float64)
    size = float(len(data))
    probabilities = histogram[histogram > 0] / size if size else histogram[:0]
    entropy = float(-np.sum(probabilities * np.log2(probabilities)))

    ascii_count = float(histogram[:128].sum())
    digit_count = float(histogram[ord("0") : ord("9") + 1].sum())
    upper_count = float(histogram[ord("A") : ord("Z") + 1].sum())
    lower_count = float(histogram[ord("a") : ord("z") + 1].sum())
    whitespace_count = float(
        histogram[ord(" ")]+histogram[ord("\t")]+histogram[ord("\n")]+histogram[ord("\r")]
    )
    words = WORD_PATTERN.findall(data)
    numbers = NUMBER_PATTERN.findall(data)
    word_bytes = sum(map(len, words))

    values = [
        math.log2(size + 1.0),
        entropy,
        safe_ratio(ascii_count, size),
        safe_ratio(size - ascii_count, size),
        safe_ratio(digit_count, size),
        safe_ratio(upper_count, size),
        safe_ratio(lower_count, size),
        safe_ratio(whitespace_count, size),
        safe_ratio(len(words), size / 1024.0),
        safe_ratio(word_bytes, len(words)),
        safe_ratio(len(numbers), len(words) + len(numbers)),
        safe_ratio(data.count(b"\n"), size / 1024.0),
    ]
    values.extend(safe_ratio(data.count(pattern), size / 1024.0) for _, pattern in TOKEN_PATTERNS)
    values.extend(safe_ratio(histogram[index], ascii_count) for index in range(128))
    return values


def feature_names() -> list[str]:
    names = [
        "log2_bytes",
        "byte_entropy",
        "ascii_ratio",
        "non_ascii_ratio",
        "digit_ratio",
        "upper_ratio",
        "lower_ratio",
        "whitespace_ratio",
        "words_per_kib",
        "average_word_length",
        "number_token_ratio",
        "lines_per_kib",
    ]
    names.extend(f"{name}_per_kib" for name, _ in TOKEN_PATTERNS)
    names.extend(f"ascii_{index:03d}_ratio" for index in range(128))
    return names


def write_article(
    output,
    article_index: int,
    nonredirect_index: int,
    data: bytes,
    redirect: bool,
) -> None:
    values = article_features(data)
    row = [
        str(article_index),
        str(nonredirect_index),
        "1" if redirect else "0",
        *(format(value, ".9g") for value in values),
    ]
    output.write("\t".join(row))
    output.write("\n")


def extract(args: argparse.Namespace) -> tuple[int, int]:
    article_index = -1
    nonredirect_index = 0
    page = bytearray()
    redirect = False
    in_page = False

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.input.open("rb") as source, args.output.open("w", encoding="utf-8") as output:
        output.write(
            "\t".join(
                ["article_index", "nonredirect_index", "is_redirect", *feature_names()]
            )
        )
        output.write("\n")

        for line in source:
            if line == PAGE_START:
                if in_page:
                    raise ValueError(f"nested <page> before article {article_index} ended")
                article_index += 1
                page.clear()
                redirect = False
                in_page = True

            if not in_page:
                continue

            page.extend(line)
            if any(line.startswith(prefix) for prefix in REDIRECT_PREFIXES):
                redirect = True

            if line == PAGE_END:
                if args.include_redirects or not redirect:
                    write_article(
                        output,
                        article_index,
                        nonredirect_index,
                        bytes(page),
                        redirect,
                    )
                if not redirect:
                    nonredirect_index += 1
                in_page = False
                if args.progress_every and (article_index + 1) % args.progress_every == 0:
                    print(
                        f"processed {article_index + 1} articles",
                        file=sys.stderr,
                        flush=True,
                    )

    if in_page:
        raise ValueError(f"unterminated article {article_index}")
    return article_index + 1, nonredirect_index


def main() -> int:
    args = parse_args()
    article_count, nonredirect_count = extract(args)
    print(
        f"wrote {nonredirect_count} non-redirect features from {article_count} articles",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
