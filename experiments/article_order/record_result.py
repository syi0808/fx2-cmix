#!/usr/bin/env python3

import argparse
import csv
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Record comparable fx2-cmix article-order experiment metrics."
    )
    parser.add_argument("--name", required=True)
    parser.add_argument("--csv", required=True, type=Path)
    parser.add_argument("--order", type=Path)
    parser.add_argument("--compressed-order", type=Path)
    parser.add_argument("--body", type=Path)
    parser.add_argument("--compressor", type=Path)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--original", type=Path)
    parser.add_argument("--restored", type=Path)
    parser.add_argument("--compression-seconds", type=float)
    parser.add_argument("--decompression-seconds", type=float)
    parser.add_argument("--peak-rss-kib", type=int)
    parser.add_argument("--notes", default="")
    return parser.parse_args()


def file_size(path: Path | None) -> int | None:
    return path.stat().st_size if path is not None else None


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    args = parse_args()
    if (args.original is None) != (args.restored is None):
        raise ValueError("--original and --restored must be supplied together")

    compressor_bytes = file_size(args.compressor)
    archive_bytes = file_size(args.archive)
    row = {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "name": args.name,
        "order_bytes": file_size(args.order),
        "compressed_order_bytes": file_size(args.compressed_order),
        "body_bytes": file_size(args.body),
        "compressor_bytes": compressor_bytes,
        "archive_bytes": archive_bytes,
        "total_submission_bytes": (
            compressor_bytes + archive_bytes
            if compressor_bytes is not None and archive_bytes is not None
            else None
        ),
        "compression_seconds": args.compression_seconds,
        "decompression_seconds": args.decompression_seconds,
        "peak_rss_kib": args.peak_rss_kib,
        "restored_sha256_matches": (
            sha256(args.original) == sha256(args.restored)
            if args.original is not None
            else None
        ),
        "notes": args.notes,
    }

    args.csv.parent.mkdir(parents=True, exist_ok=True)
    write_header = not args.csv.exists()
    with args.csv.open("a", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=row.keys())
        if write_header:
            writer.writeheader()
        writer.writerow(row)
    print(json.dumps(row, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
