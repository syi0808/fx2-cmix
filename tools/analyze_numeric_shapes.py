#!/usr/bin/env python3

import argparse
import json
import mmap
import re
from collections import Counter
from pathlib import Path


SEQUENCE_RE = re.compile(
    rb"[0-9]+(?:(?:[-J:.,/+][0-9]+)|(?:T[0-9]+))*"
)
RUN_RE = re.compile(rb"[0-9]+")
WRT_RESTORE = bytes.maketrans(
    b"JKLMNOPQR",
    b":;<=>?{|}",
)


def readable(value):
    restored = value.translate(WRT_RESTORE)
    return "".join(chr(byte) if 32 <= byte < 127 else f"\\x{byte:02x}"
                   for byte in restored)


def normalized(value):
    restored = value.translate(WRT_RESTORE)
    return re.sub(r"[0-9]", "D", restored.decode("latin1"))


def top(counter, limit):
    return [
        {"pattern": key, "count": count}
        for key, count in counter.most_common(limit)
    ]


def analyze(path, limit):
    run_lengths = Counter()
    left_contexts = Counter()
    right_contexts = Counter()
    shapes = Counter()
    previous_words = Counter()
    separators = Counter()
    digit_bytes = 0
    run_count = 0

    with path.open("rb") as input_file:
        with mmap.mmap(input_file.fileno(), 0, access=mmap.ACCESS_READ) as data:
            for match in RUN_RE.finditer(data):
                run_count += 1
                run_length = match.end() - match.start()
                digit_bytes += run_length
                run_lengths[str(run_length)] += 1
                left = data[max(0, match.start() - 4):match.start()]
                right = data[match.end():match.end() + 4]
                left_contexts[readable(left)] += 1
                right_contexts[readable(right)] += 1
                word_match = re.search(rb"[A-Za-z\x80-\xff]+$", left)
                if word_match:
                    previous_words[readable(word_match.group())] += 1

            for match in SEQUENCE_RE.finditer(data):
                value = match.group()
                shape = normalized(value)
                shapes[shape] += 1
                for byte in value:
                    if byte in b"-J:.,/+":
                        separators[readable(bytes([byte]))] += 1

    return {
        "path": str(path),
        "predictor_input_bytes": path.stat().st_size,
        "numeric_runs": run_count,
        "digit_bytes": digit_bytes,
        "run_lengths": top(run_lengths, limit),
        "left_contexts": top(left_contexts, limit),
        "right_contexts": top(right_contexts, limit),
        "sequence_shapes": top(shapes, limit),
        "separators": top(separators, limit),
        "previous_words": top(previous_words, limit),
    }


def print_report(report):
    print(f"Predictor input: {report['predictor_input_bytes']:,} bytes")
    print(f"Numeric runs: {report['numeric_runs']:,}")
    print(f"Digit bytes: {report['digit_bytes']:,}")
    for title, key in (
        ("Run lengths", "run_lengths"),
        ("Sequence shapes", "sequence_shapes"),
        ("Left contexts", "left_contexts"),
        ("Right contexts", "right_contexts"),
        ("Separators", "separators"),
        ("Previous words", "previous_words"),
    ):
        print(f"\n{title}")
        for item in report[key]:
            print(f"{item['count']:>12,}  {item['pattern']}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("predictor_input", type=Path)
    parser.add_argument("--limit", type=int, default=30)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    report = analyze(args.predictor_input, args.limit)
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print_report(report)


if __name__ == "__main__":
    main()
