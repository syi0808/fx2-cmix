#!/usr/bin/env python3

import argparse
from pathlib import Path


URL_MARKERS = (b"http://", b"https://", b"www.")


def copy_slice(source, output, offset, byte_count):
    source.seek(offset)
    remaining = byte_count
    while remaining:
        chunk = source.read(min(1024 * 1024, remaining))
        if not chunk:
            break
        output.write(chunk)
        remaining -= len(chunk)


def copy_url_free(source, output, byte_count):
    written = 0
    for line in source:
        lowered = line.lower()
        if any(marker in lowered for marker in URL_MARKERS):
            continue
        chunk = line[:byte_count - written]
        output.write(chunk)
        written += len(chunk)
        if written == byte_count:
            break


def main():
    parser = argparse.ArgumentParser(
        description="Build contiguous or URL-free enwik benchmark corpora."
    )
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--bytes", type=int, default=10 * 1024 * 1024)
    parser.add_argument("--offset", type=int, default=0)
    parser.add_argument("--url-free", action="store_true")
    args = parser.parse_args()

    with args.input.open("rb") as source, args.output.open("wb") as output:
        if args.url_free:
            copy_url_free(source, output, args.bytes)
        else:
            copy_slice(source, output, args.offset, args.bytes)

    if args.output.stat().st_size != args.bytes:
        raise SystemExit("input ended before requested corpus size")


if __name__ == "__main__":
    main()
