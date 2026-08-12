#!/usr/bin/env python3
import csv
import math
import sys
from pathlib import Path

if len(sys.argv) != 4:
    raise SystemExit("usage: factorization_analyze.py FIRST.csv SECOND.csv OUT.csv")

first_path, second_path, out_path = map(Path, sys.argv[1:])

# Exact 4-byte PPMD joint ranks measured by the private-MAP PPMD branch-and-
# bound oracle. The first eight came from the 4-byte smoke run; the hard tail at
# 4680 was re-run to completion with a 500k state-fork budget.
JOINT_RANK = {
    1024: 1,
    1481: 1,
    1938: 1,
    2395: 1,
    2852: 10,
    3309: 1,
    3766: 1,
    4223: 1,
    4680: 331488,
}


def read_rows(path):
    rows = {}
    with path.open(newline="") as handle:
        reader = csv.DictReader(
            line for line in handle if line.strip() and not line.startswith("SUMMARY")
        )
        for row in reader:
            rows[int(row["position"])] = row
    return rows


first = read_rows(first_path)
second = read_rows(second_path)

fieldnames = [
    "position",
    "first_pair_rank",
    "second_pair_rank",
    "product_rank",
    "sum_local_log2_rank",
    "joint_rank",
    "joint_log2_rank",
    "joint_minus_local_bits",
    "joint_over_product",
]

rows = []
for position, joint_rank in JOINT_RANK.items():
    if position not in first or position + 2 not in second:
        raise SystemExit(f"missing pair measurement for position {position}")
    r1 = int(first[position]["rank"])
    r2 = int(second[position + 2]["rank"])
    product = r1 * r2
    local_bits = math.log2(product)
    joint_bits = math.log2(joint_rank)
    rows.append({
        "position": position,
        "first_pair_rank": r1,
        "second_pair_rank": r2,
        "product_rank": product,
        "sum_local_log2_rank": f"{local_bits:.9f}",
        "joint_rank": joint_rank,
        "joint_log2_rank": f"{joint_bits:.9f}",
        "joint_minus_local_bits": f"{joint_bits - local_bits:.9f}",
        "joint_over_product": f"{joint_rank / product:.9f}",
    })

with out_path.open("w", newline="") as handle:
    writer = csv.DictWriter(handle, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)

local_total = sum(float(row["sum_local_log2_rank"]) for row in rows)
joint_total = sum(float(row["joint_log2_rank"]) for row in rows)
hard = next(row for row in rows if int(row["position"]) == 4680)

print(f"samples={len(rows)}")
print(f"sum_local_log2_rank_bits={local_total:.9f}")
print(f"sum_joint_log2_rank_bits={joint_total:.9f}")
print(f"mean_joint_minus_local_bits={(joint_total-local_total)/len(rows):.9f}")
print(f"hard_4680_local_bits={hard['sum_local_log2_rank']}")
print(f"hard_4680_joint_bits={hard['joint_log2_rank']}")
print(f"hard_4680_delta_bits={hard['joint_minus_local_bits']}")
print(f"hard_4680_joint_over_product={hard['joint_over_product']}")
