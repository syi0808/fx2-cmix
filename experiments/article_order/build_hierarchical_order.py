#!/usr/bin/env python3

import argparse
import math
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np


@dataclass(frozen=True)
class BlockSpec:
    path: Path
    weight: float
    id_offset: int


def block_spec(value: str) -> BlockSpec:
    parts = value.rsplit(":", 2)
    try:
        if len(parts) == 1:
            spec = BlockSpec(Path(parts[0]), 1.0, 0)
        elif len(parts) == 2:
            spec = BlockSpec(Path(parts[0]), float(parts[1]), 0)
        else:
            spec = BlockSpec(Path(parts[0]), float(parts[1]), int(parts[2]))
        if spec.weight <= 0:
            raise ValueError
        return spec
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "block must be PATH, PATH:WEIGHT, or PATH:WEIGHT:ID_OFFSET"
        ) from error


def parse_sizes(value: str) -> tuple[int, ...]:
    try:
        sizes = tuple(int(part) for part in value.split(","))
    except ValueError as error:
        raise argparse.ArgumentTypeError("block sizes must be comma-separated integers") from error
    if not sizes or any(size <= 0 for size in sizes):
        raise argparse.ArgumentTypeError("block sizes must be positive")
    if any(parent % child for parent, child in zip(sizes, sizes[1:])):
        raise argparse.ArgumentTypeError("each block size must be divisible by the next")
    if any(parent <= child for parent, child in zip(sizes, sizes[1:])):
        raise argparse.ArgumentTypeError("block sizes must be strictly descending")
    return sizes


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build a fixed-boundary hierarchical balanced article order."
    )
    parser.add_argument(
        "--block",
        action="append",
        required=True,
        type=block_spec,
        help="feature TSV as PATH[:WEIGHT[:ID_OFFSET]]; repeat for multiple blocks",
    )
    parser.add_argument("--output", required=True, type=Path, help="article order output")
    parser.add_argument(
        "--block-sizes",
        type=parse_sizes,
        default=(4096, 512, 64),
        help="descending hierarchy, default: 4096,512,64",
    )
    parser.add_argument(
        "--max-dimensions",
        type=int,
        default=128,
        help="retain at most this many high-variance dimensions per feature block",
    )
    parser.add_argument(
        "--split-iterations",
        type=int,
        default=2,
        help="balanced two-means refinement iterations",
    )
    return parser.parse_args()


def numeric_line(line: str) -> np.ndarray:
    values = np.fromstring(line, sep="\t", dtype=np.float64)
    if values.size < 2:
        raise ValueError("feature rows must contain an id and at least one feature")
    return values


def load_block(spec: BlockSpec) -> tuple[np.ndarray, np.ndarray]:
    ids: list[int] = []
    rows: list[np.ndarray] = []
    with spec.path.open("r", encoding="utf-8") as source:
        first = source.readline()
        if not first:
            raise ValueError(f"empty feature block: {spec.path}")
        first_field = first.split("\t", 1)[0]
        has_header = first_field in {"article_index", "id"}
        selected_columns = None
        if has_header:
            columns = first.rstrip("\n").split("\t")
            selected_columns = [
                index
                for index, name in enumerate(columns)
                if name not in {"article_index", "id", "nonredirect_index", "is_redirect"}
            ]
            if not selected_columns:
                raise ValueError(f"feature block has no feature columns: {spec.path}")
        if not has_header:
            values = numeric_line(first)
            ids.append(int(values[0]) + spec.id_offset)
            rows.append(values[1:])
        for line_number, line in enumerate(source, start=2):
            if not line.strip():
                continue
            values = numeric_line(line)
            ids.append(int(values[0]) + spec.id_offset)
            row = values[selected_columns] if selected_columns is not None else values[1:]
            if rows and row.size != rows[0].size:
                raise ValueError(f"inconsistent dimensions at {spec.path}:{line_number}")
            rows.append(row)
    if not rows:
        raise ValueError(f"feature block has no data rows: {spec.path}")
    if len(ids) != len(set(ids)):
        raise ValueError(f"feature block contains duplicate article ids: {spec.path}")
    if any(article_id < 0 for article_id in ids):
        raise ValueError(f"feature block contains negative article ids: {spec.path}")
    return np.asarray(ids, dtype=np.int64), np.asarray(rows, dtype=np.float32)


def align_blocks(
    loaded: list[tuple[np.ndarray, np.ndarray]],
) -> tuple[np.ndarray, list[np.ndarray]]:
    common = set(map(int, loaded[0][0]))
    for ids, _ in loaded[1:]:
        common.intersection_update(map(int, ids))
    if not common:
        raise ValueError("feature blocks have no article ids in common")
    article_ids = np.asarray(sorted(common), dtype=np.int64)
    matrices = []
    for ids, matrix in loaded:
        positions = {int(article_id): index for index, article_id in enumerate(ids)}
        matrices.append(matrix[[positions[int(article_id)] for article_id in article_ids]])
    return article_ids, matrices


def prepare_block(matrix: np.ndarray, weight: float, max_dimensions: int) -> np.ndarray:
    finite = np.isfinite(matrix)
    if not finite.all():
        matrix = matrix.copy()
        matrix[~finite] = 0
    means = matrix.mean(axis=0, dtype=np.float64)
    variances = matrix.var(axis=0, dtype=np.float64)
    useful = np.flatnonzero(variances > 1e-12)
    if useful.size == 0:
        raise ValueError("feature block contains no varying dimensions")
    if useful.size > max_dimensions:
        useful = useful[np.argsort(variances[useful], kind="stable")[-max_dimensions:]]
    matrix = matrix[:, useful]
    means = means[useful]
    standard_deviations = np.sqrt(variances[useful])
    matrix = (matrix - means) / standard_deviations
    norms = np.linalg.norm(matrix, axis=1)
    norms[norms == 0] = 1
    return np.asarray(matrix / norms[:, None] * weight, dtype=np.float32)


def balanced_split(
    indices: np.ndarray,
    left_size: int,
    features: np.ndarray,
    article_ids: np.ndarray,
    iterations: int,
) -> tuple[np.ndarray, np.ndarray]:
    subset = features[indices]
    variances = subset.var(axis=0, dtype=np.float64)
    axis = int(np.argmax(variances))
    axis_order = np.argsort(subset[:, axis], kind="stable")
    left_center = subset[axis_order[max(0, left_size // 2)]].astype(np.float64)
    right_center = subset[axis_order[min(len(indices) - 1, left_size + (len(indices) - left_size) // 2)]].astype(np.float64)

    order = axis_order
    for _ in range(max(1, iterations)):
        delta = (
            np.sum((subset - left_center) ** 2, axis=1)
            - np.sum((subset - right_center) ** 2, axis=1)
        )
        order = np.lexsort((article_ids[indices], delta))
        left_center = subset[order[:left_size]].mean(axis=0, dtype=np.float64)
        right_center = subset[order[left_size:]].mean(axis=0, dtype=np.float64)
    return indices[order[:left_size]], indices[order[left_size:]]


def fixed_blocks(
    indices: np.ndarray,
    block_size: int,
    features: np.ndarray,
    article_ids: np.ndarray,
    iterations: int,
) -> list[np.ndarray]:
    block_count = math.ceil(len(indices) / block_size)
    if block_count <= 1:
        return [indices]
    left_blocks = block_count // 2
    left_size = left_blocks * block_size
    left, right = balanced_split(
        indices, left_size, features, article_ids, iterations
    )
    return [
        *fixed_blocks(left, block_size, features, article_ids, iterations),
        *fixed_blocks(right, block_size, features, article_ids, iterations),
    ]


def hierarchical_order(
    article_ids: np.ndarray,
    features: np.ndarray,
    block_sizes: tuple[int, ...],
    iterations: int,
) -> np.ndarray:
    groups = [np.arange(len(article_ids), dtype=np.int64)]
    for block_size in block_sizes:
        next_groups = []
        for group in groups:
            next_groups.extend(
                fixed_blocks(group, block_size, features, article_ids, iterations)
            )
        groups = next_groups
        print(f"{block_size}: {len(groups)} groups", file=sys.stderr)
    ordered = []
    for group in groups:
        ordered.extend(group[np.argsort(article_ids[group], kind="stable")])
    return article_ids[np.asarray(ordered, dtype=np.int64)]


def main() -> int:
    args = parse_args()
    if args.max_dimensions <= 0:
        raise ValueError("--max-dimensions must be positive")
    loaded = [load_block(spec) for spec in args.block]
    article_ids, matrices = align_blocks(loaded)
    prepared = [
        prepare_block(matrix, spec.weight, args.max_dimensions)
        for spec, matrix in zip(args.block, matrices)
    ]
    features = np.concatenate(prepared, axis=1)
    order = hierarchical_order(
        article_ids, features, args.block_sizes, args.split_iterations
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as output:
        for article_id in order:
            output.write(f"{article_id}\n")
    print(
        f"wrote {len(order)} articles using {features.shape[1]} dimensions",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
