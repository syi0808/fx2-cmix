#!/usr/bin/env python3

import argparse
import json
import mmap
import math
import re
from collections import Counter, defaultdict
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


def run_length_bucket(length):
    if length <= 4:
        return str(length)
    if length <= 6:
        return "5-6"
    if length <= 8:
        return "7-8"
    return "9+"


def structural_class(byte):
    if byte is None:
        return "other"
    if byte in (10, 13):
        return "newline"
    if byte in (9, 32):
        return "whitespace"
    if byte in b"<L{P":
        return "open_markup"
    if byte in b">N}R":
        return "close_markup"
    if byte in b"=M":
        return "equals"
    if byte in b"|Q":
        return "pipe"
    if byte in b":J":
        return "colon"
    if byte in b";K":
        return "semicolon"
    if 65 <= byte <= 90 or 97 <= byte <= 122:
        return "letter"
    if byte >= 128:
        return "word_token"
    return "other"


def terminator_class(byte):
    if byte is None:
        return "eof"
    structural = structural_class(byte)
    if structural in ("whitespace", "newline"):
        return "space"
    if structural in ("open_markup", "close_markup"):
        return "markup"
    return {
        ord("-"): "dash",
        ord(":"): "colon",
        ord("J"): "colon",
        ord("."): "dot",
        ord(","): "comma",
        ord("/"): "slash",
    }.get(byte, "other")


def entropy(counter):
    total = sum(counter.values())
    if not total:
        return 0.0
    return -sum(
        count / total * math.log2(count / total)
        for count in counter.values()
    )


def conditional_entropy(groups):
    total = sum(sum(counter.values()) for counter in groups.values())
    if not total:
        return 0.0
    return sum(
        sum(counter.values()) / total * entropy(counter)
        for counter in groups.values()
    )


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
    terminators = Counter()
    terminator_bytes = Counter()
    by_run_length = defaultdict(Counter)
    by_length_structure = defaultdict(Counter)
    by_shape_prefix = defaultdict(Counter)
    digit_bytes = 0
    run_count = 0

    with path.open("rb") as input_file:
        with mmap.mmap(input_file.fileno(), 0, access=mmap.ACCESS_READ) as data:
            shape_prefix = ""
            sequence_start_class = "other"
            linked_start = None
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

                if linked_start != match.start():
                    shape_prefix = ""
                    sequence_start_class = structural_class(
                        data[match.start() - 1] if match.start() else None
                    )
                terminator_byte = (
                    data[match.end()] if match.end() < len(data) else None
                )
                terminator = terminator_class(terminator_byte)
                terminator_value = (
                    readable(bytes([terminator_byte]))
                    if terminator_byte is not None else "EOF"
                )
                length_bucket = run_length_bucket(run_length)
                terminators[terminator] += 1
                terminator_bytes[terminator_value] += 1
                by_run_length[length_bucket][terminator_value] += 1
                by_length_structure[
                    (length_bucket, sequence_start_class)
                ][terminator_value] += 1
                by_shape_prefix[
                    shape_prefix or f"start:{length_bucket}"
                ][terminator_value] += 1

                linked_start = None
                if terminator_byte is not None:
                    next_position = match.end() + 1
                    separator = readable(bytes([terminator_byte]))
                    if (terminator_byte in b"-J:.,/+" and
                            next_position < len(data) and
                            48 <= data[next_position] <= 57):
                        linked_start = next_position
                    elif (terminator_byte == ord("T") and
                          next_position < len(data) and
                          48 <= data[next_position] <= 57):
                        linked_start = next_position
                    if linked_start is not None:
                        component = f"{length_bucket}-{separator}"
                        shape_prefix = (
                            f"{shape_prefix}/{component}"
                            if shape_prefix else component
                        )

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
        "terminators": top(terminators, limit),
        "terminator_bytes": top(terminator_bytes, limit),
        "entropy_bits": {
            "terminator_byte": entropy(terminator_bytes),
            "terminator_byte_given_run_length":
                conditional_entropy(by_run_length),
            "terminator_byte_given_run_length_and_structure":
                conditional_entropy(by_length_structure),
            "terminator_byte_given_shape_prefix":
                conditional_entropy(by_shape_prefix),
            "terminator_class": entropy(terminators),
        },
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
        ("Terminators", "terminators"),
        ("Terminator bytes", "terminator_bytes"),
        ("Previous words", "previous_words"),
    ):
        print(f"\n{title}")
        for item in report[key]:
            print(f"{item['count']:>12,}  {item['pattern']}")
    print("\nEmpirical terminator entropy")
    for name, value in report["entropy_bits"].items():
        print(f"{name:>44}: {value:.6f} bits")


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
