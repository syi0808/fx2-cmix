#!/usr/bin/env python3

import argparse
import csv
import os
import subprocess
import time
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run full Predictor edge probes.")
    parser.add_argument("--index", required=True, type=Path)
    parser.add_argument("--cmix", required=True, type=Path)
    parser.add_argument("--dictionary", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    with args.index.open("r", encoding="utf-8") as source:
        rows = list(csv.DictReader(source, delimiter="\t"))
    for index, row in enumerate(rows, 1):
        output_path = Path(row["trace"]).with_suffix(".cmix")
        environment = os.environ.copy()
        environment.update(
            {
                "FX2_SKIP_PRETRAIN": "1",
                "FX2_EXPERIMENT_TRACE_PATH": row["trace"],
                "FX2_ARTICLE_MANIFEST": row["manifest"],
            }
        )
        start = time.monotonic()
        result = subprocess.run(
            [
                str(args.cmix.resolve()),
                "-r",
                str(args.dictionary.resolve()),
                str(Path(row["input"]).resolve()),
                str(output_path.resolve()),
            ],
            env=environment,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        output_path.unlink(missing_ok=True)
        Path(str(output_path) + ".cmix.temp").unlink(missing_ok=True)
        if result.returncode:
            raise RuntimeError(f"probe {row['name']} exited {result.returncode}")
        print(f"[{index}/{len(rows)}] {row['name']} {time.monotonic() - start:.2f}s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
