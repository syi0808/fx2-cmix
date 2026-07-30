#!/usr/bin/env python3
from __future__ import annotations

import collections
import json
import math
import mmap
import re
import sys
from pathlib import Path

DIGIT_RE = re.compile(rb"[0-9]+")
ISO_TS_RE = re.compile(rb"<timestamp>[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z</timestamp>")
ID_RE = re.compile(rb"<id>([0-9]+)</id>")
NUMERIC_ENTITY_RE = re.compile(rb"(?:&amp;#|&#)([0-9]+);")
HEX_RE = re.compile(rb"(?<![0-9A-Za-z])[0-9A-Fa-f]{8,}(?![0-9A-Za-z])")


def pct(value: int, total: int) -> float:
    return value * 100.0 / total if total else 0.0


def theoretical_savings(byte_count: int) -> dict[str, int]:
    return {
        f"{delta:g}_bpb": round(byte_count * delta / 8.0)
        for delta in (0.1, 0.25, 0.5, 1.0, 2.0)
    }


def main() -> int:
    path = Path(sys.argv[1] if len(sys.argv) > 1 else "enwik9")
    if not path.is_file():
        raise SystemExit(f"not found: {path}")

    with path.open("rb") as f:
        data = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        total = len(data)

        digit_freq = {str(i): data.count(bytes((48 + i,))) for i in range(10)}
        digit_bytes = sum(digit_freq.values())

        run_count = 0
        run_bytes = 0
        run_len_hist: collections.Counter[int] = collections.Counter()
        run_threshold_counts = {n: {"runs": 0, "bytes": 0} for n in (2, 4, 6, 8, 10, 16)}
        for match in DIGIT_RE.finditer(data):
            length = match.end() - match.start()
            run_count += 1
            run_bytes += length
            run_len_hist[min(length, 32)] += 1
            for threshold, stats in run_threshold_counts.items():
                if length >= threshold:
                    stats["runs"] += 1
                    stats["bytes"] += length

        timestamps = list(ISO_TS_RE.finditer(data))
        timestamp_region_bytes = sum(m.end() - m.start() for m in timestamps)
        timestamp_digit_bytes = len(timestamps) * 14

        ids = list(ID_RE.finditer(data))
        id_digit_bytes = sum(len(m.group(1)) for m in ids)
        id_length_hist = collections.Counter(len(m.group(1)) for m in ids)

        entities = list(NUMERIC_ENTITY_RE.finditer(data))
        entity_digit_bytes = sum(len(m.group(1)) for m in entities)

        hex_matches = []
        hex_bytes = 0
        for match in HEX_RE.finditer(data):
            token = match.group(0)
            if any(c in b"abcdefABCDEF" for c in token) and any(48 <= c <= 57 for c in token):
                hex_matches.append(match)
                hex_bytes += len(token)

        # Digit bytes can overlap among these semantic subsets. This is intentional.
        result = {
            "file": {
                "path": str(path),
                "bytes": total,
            },
            "ascii_digits": {
                "bytes": digit_bytes,
                "ratio": digit_bytes / total,
                "percent": pct(digit_bytes, total),
                "frequency": digit_freq,
                "runs": run_count,
                "mean_run_length": run_bytes / run_count if run_count else 0.0,
                "run_length_histogram_1_to_31_plus": {
                    ("32+" if k == 32 else str(k)): v for k, v in sorted(run_len_hist.items())
                },
                "thresholds": run_threshold_counts,
                "theoretical_savings_by_numeric_byte_improvement": theoretical_savings(digit_bytes),
                "raw_decimal_alphabet_ceiling": {
                    "ideal_uniform_bits_per_digit": math.log2(10),
                    "saving_vs_8_raw_bits_per_digit": digit_bytes * (8.0 - math.log2(10)) / 8.0,
                    "warning": "Raw representation ceiling, not incremental gain over fx2-cmix.",
                },
            },
            "iso_timestamps": {
                "count": len(timestamps),
                "full_region_bytes": timestamp_region_bytes,
                "digit_bytes": timestamp_digit_bytes,
                "digit_ratio_of_file": timestamp_digit_bytes / total,
                "theoretical_savings_by_digit_improvement": theoretical_savings(timestamp_digit_bytes),
            },
            "xml_ids": {
                "count": len(ids),
                "digit_bytes": id_digit_bytes,
                "digit_ratio_of_file": id_digit_bytes / total,
                "length_histogram": dict(sorted(id_length_hist.items())),
                "theoretical_savings_by_digit_improvement": theoretical_savings(id_digit_bytes),
            },
            "numeric_entities": {
                "count": len(entities),
                "digit_bytes": entity_digit_bytes,
                "digit_ratio_of_file": entity_digit_bytes / total,
                "theoretical_savings_by_digit_improvement": theoretical_savings(entity_digit_bytes),
            },
            "hex_candidates": {
                "count": len(hex_matches),
                "bytes": hex_bytes,
                "ratio_of_file": hex_bytes / total,
                "theoretical_savings_by_byte_improvement": theoretical_savings(hex_bytes),
            },
            "notes": [
                "Semantic subsets overlap the global ASCII digit count.",
                "ISO timestamp digit count is 14 digits per exact MediaWiki timestamp.",
                "XML ID count includes page, revision, and contributor IDs because raw enwik9 uses the same <id> tag.",
                "Hex candidates require length >= 8 and at least one decimal digit and one A-F letter.",
            ],
        }

    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
