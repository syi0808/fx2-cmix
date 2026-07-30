#!/usr/bin/env python3

import argparse
from pathlib import Path


URLS = [
    "https://en.wikipedia.org/wiki/Compression",
    "https://en.wikipedia.org/wiki/Entropy",
    "https://upload.wikimedia.org/wikipedia/commons/a/a1/foo.png",
    "https://upload.wikimedia.org/wikipedia/commons/b/b2/bar.png",
    "https://example.com/item/{number}",
    "https://example.com/archive/{year}/{month:02d}/{day:02d}/file",
    "https://example.com/index.php?title=Entropy&action=edit",
    "https://example.com/index.php?title=Compression&action=view",
    "https://example.com/search?q=entropy&page={page}",
    "https://example.com/search?q=compressor&page={page}",
    "//cdn.example.org/image.png",
    "www.example.com/path",
    "https://example.com/a%20b",
    "https://example.com/Foo_(bar)",
    "https://user@example.com:8080/path",
    "https://[2001:db8::1]/index.html",
]


def build(target_bytes):
    output = bytearray()
    index = 0
    while len(output) < target_bytes:
        url = URLS[index % len(URLS)].format(
            number=10000 + (index * 7919) % 90000,
            year=2024 + index % 3,
            month=1 + index % 12,
            day=1 + index % 28,
            page=1 + index % 20,
        )
        line = (
            f"Reference {index}: {url}. "
            f"[{url} label] url={url}|title=Example\n"
        )
        output.extend(line.encode())
        index += 1
    return output[:target_bytes]


def main():
    parser = argparse.ArgumentParser(
        description="Build a deterministic URL-heavy Predictor fixture."
    )
    parser.add_argument("output", type=Path)
    parser.add_argument("--bytes", type=int, default=262144)
    args = parser.parse_args()
    args.output.write_bytes(build(args.bytes))


if __name__ == "__main__":
    main()
