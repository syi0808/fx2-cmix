#!/usr/bin/env python3

import argparse
import csv
import gzip
import json
import math
import random
from collections import defaultdict
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Optimize bounded article order with a causal n-gram probe."
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--tail-bytes", type=int, default=16384)
    parser.add_argument("--probe-bytes", type=int, default=4096)
    parser.add_argument("--block-size", type=int, default=32)
    parser.add_argument("--probe-targets", type=int, default=12)
    parser.add_argument("--seed", type=int, default=923)
    parser.add_argument(
        "--strategy", choices=("adjacent", "greedy"), default="adjacent"
    )
    return parser.parse_args()


def load_articles(input_path: Path, manifest_path: Path):
    data = input_path.read_bytes()
    articles = {}
    order = []
    splits = {}
    with manifest_path.open("r", encoding="utf-8") as source:
        for row in csv.DictReader(source, delimiter="\t"):
            article_id = int(row["article_id"])
            begin = int(row["stream_begin"])
            end = int(row["stream_end"])
            articles[article_id] = data[begin:end]
            splits[article_id] = int(row["split"])
            order.append(article_id)
    return articles, order, splits


def build_tail_model(data: bytes, tail_bytes: int):
    context_counts = defaultdict(int)
    ngram_counts = defaultdict(int)
    tail = data[-tail_bytes:]
    for index in range(2, len(tail)):
        context = (tail[index - 2] << 8) | tail[index - 1]
        context_counts[context] += 1
        ngram_counts[(context << 8) | tail[index]] += 1
    return context_counts, ngram_counts


def score_successor(model, data: bytes, probe_bytes: int) -> float:
    context_counts, ngram_counts = model
    local_context = defaultdict(int)
    local_ngram = defaultdict(int)
    prefix = data[:probe_bytes]
    loss = 0.0
    for index in range(2, len(prefix)):
        context = (prefix[index - 2] << 8) | prefix[index - 1]
        ngram = (context << 8) | prefix[index]
        total = context_counts.get(context, 0) + local_context[context]
        count = ngram_counts.get(ngram, 0) + local_ngram[ngram]
        probability = (count + 0.5) / (total + 128.0)
        loss -= math.log2(probability)
        local_context[context] += 1
        local_ngram[ngram] += 1
    return loss


def edge_scores(articles, order, tail_bytes, probe_bytes, block_size, seed):
    rng = random.Random(seed)
    scores = {}
    random_edges = []
    for block_begin in range(0, len(order), block_size):
        block = order[block_begin:block_begin + block_size]
        for source_id in block:
            model = build_tail_model(articles[source_id], tail_bytes)
            for target_id in block:
                if source_id != target_id:
                    scores[(source_id, target_id)] = score_successor(
                        model, articles[target_id], probe_bytes
                    )
            controls = rng.sample(
                [article_id for article_id in order if article_id not in block],
                min(2, len(order) - len(block)),
            )
            for target_id in controls:
                random_edges.append(
                    (source_id, target_id,
                     score_successor(model, articles[target_id], probe_bytes))
                )
    return scores, random_edges


def path_score(order, scores):
    return sum(scores.get(edge, 0.0) for edge in zip(order, order[1:]))


def optimize_block(block, scores, strategy):
    baseline_score = path_score(block, scores)
    candidates = [(baseline_score, block)]
    starts = [block[0], block[-1], block[len(block) // 2]]
    for start in starts:
        unused = set(block)
        unused.remove(start)
        candidate = [start]
        while unused:
            predecessor = candidate[-1]
            successor = min(unused, key=lambda item: scores[(predecessor, item)])
            unused.remove(successor)
            candidate.append(successor)
        candidates.append((path_score(candidate, scores), candidate))

    adjacent = list(block)
    changed = True
    while changed:
        changed = False
        for index in range(len(adjacent) - 1):
            old_score = path_score(adjacent, scores)
            adjacent[index], adjacent[index + 1] = (
                adjacent[index + 1], adjacent[index]
            )
            new_score = path_score(adjacent, scores)
            if new_score + 1e-9 < old_score:
                changed = True
            else:
                adjacent[index], adjacent[index + 1] = (
                    adjacent[index + 1], adjacent[index]
                )
    candidates.append((path_score(adjacent, scores), adjacent))
    if strategy == "adjacent":
        return path_score(adjacent, scores), adjacent
    return min(candidates, key=lambda item: item[0])


def optimize_order(order, scores, block_size, strategy):
    optimized = []
    baseline_bits = 0.0
    optimized_bits = 0.0
    for begin in range(0, len(order), block_size):
        block = order[begin:begin + block_size]
        baseline_bits += path_score(block, scores)
        score, candidate = optimize_block(block, scores, strategy)
        optimized_bits += score
        optimized.extend(candidate)
    return optimized, baseline_bits, optimized_bits


def write_corpus(path, manifest_path, articles, order, splits):
    path.parent.mkdir(parents=True, exist_ok=True)
    records = []
    with path.open("wb") as output:
        output.write(b"<mediawiki>\n")
        for rank, article_id in enumerate(order):
            begin = output.tell()
            output.write(articles[article_id])
            records.append((rank, article_id, begin, output.tell(), splits[article_id]))
        output.write(b"</mediawiki>\n")
    with manifest_path.open("w", encoding="utf-8") as output:
        output.write(
            "rank\tarticle_id\tsource_article\tstream_begin\tstream_end\tsplit\n"
        )
        for rank, article_id, begin, end, split in records:
            output.write(
                f"{rank}\t{article_id}\t{article_id}\t{begin}\t{end}\t{split}\n"
            )


def gzip_size(data: bytes) -> int:
    return len(gzip.compress(data, compresslevel=9, mtime=0))


def prepare_full_probes(output_dir, articles, baseline, optimized, scores,
                        random_edges, tail_bytes, probe_bytes, target_count):
    probe_dir = output_dir / "full-probes"
    probe_dir.mkdir(parents=True, exist_ok=True)
    optimized_position = {article_id: index for index, article_id in enumerate(optimized)}
    random_by_target = {}
    for source_id, target_id, score in random_edges:
        random_by_target.setdefault(target_id, (source_id, score))
    rows = []
    stride = max(1, (len(baseline) - 1) // target_count)
    for target_index in range(stride, len(baseline), stride):
        if len(rows) >= target_count * 3:
            break
        target_id = baseline[target_index]
        optimized_index = optimized_position[target_id]
        if optimized_index == 0 or target_id not in random_by_target:
            continue
        predecessors = {
            "baseline": baseline[target_index - 1],
            "optimized": optimized[optimized_index - 1],
            "random": random_by_target[target_id][0],
        }
        for kind, source_id in predecessors.items():
            source = articles[source_id][-tail_bytes:]
            target = articles[target_id][:probe_bytes]
            name = f"{target_id}-{kind}"
            input_path = probe_dir / f"{name}.bin"
            manifest_path = probe_dir / f"{name}.manifest.tsv"
            input_path.write_bytes(source + target)
            manifest_path.write_text(
                "rank\tarticle_id\tsource_article\tstream_begin\tstream_end\tsplit\n"
                f"0\t{target_id}\t{target_id}\t{len(source)}\t"
                f"{len(source) + len(target)}\t0\n",
                encoding="utf-8",
            )
            rows.append(
                {
                    "name": name,
                    "kind": kind,
                    "source_id": source_id,
                    "target_id": target_id,
                    "proxy_bits": scores.get((source_id, target_id),
                                             random_by_target[target_id][1]),
                    "input": str(input_path),
                    "manifest": str(manifest_path),
                    "trace": str(probe_dir / f"{name}.trace.tsv"),
                }
            )
    with (probe_dir / "index.tsv").open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=rows[0].keys(), delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    articles, baseline, splits = load_articles(args.input, args.manifest)
    scores, random_edges = edge_scores(
        articles, baseline, args.tail_bytes, args.probe_bytes,
        args.block_size, args.seed
    )
    optimized, baseline_bits, optimized_bits = optimize_order(
        baseline, scores, args.block_size, args.strategy
    )
    candidate_path = args.output_dir / "causal-20m.xml"
    candidate_manifest = args.output_dir / "causal-20m.manifest.tsv"
    write_corpus(candidate_path, candidate_manifest, articles, optimized, splits)
    (args.output_dir / "causal-20m.order.txt").write_text(
        "".join(f"{article_id}\n" for article_id in optimized), encoding="utf-8"
    )
    baseline_data = args.input.read_bytes()
    candidate_data = candidate_path.read_bytes()
    baseline_order_gzip = gzip_size(
        "".join(f"{article_id}\n" for article_id in baseline).encode()
    )
    candidate_order_gzip = gzip_size(
        "".join(f"{article_id}\n" for article_id in optimized).encode()
    )
    report = {
        "articles": len(baseline),
        "tail_bytes": args.tail_bytes,
        "probe_bytes": args.probe_bytes,
        "block_size": args.block_size,
        "strategy": args.strategy,
        "moved_articles": sum(a != b for a, b in zip(baseline, optimized)),
        "proxy_baseline_bits": baseline_bits,
        "proxy_candidate_bits": optimized_bits,
        "proxy_gain_bits": baseline_bits - optimized_bits,
        "gzip_baseline_bytes": gzip_size(baseline_data),
        "gzip_candidate_bytes": gzip_size(candidate_data),
        "order_gzip_baseline_bytes": baseline_order_gzip,
        "order_gzip_candidate_bytes": candidate_order_gzip,
    }
    (args.output_dir / "proxy-report.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )
    prepare_full_probes(
        args.output_dir, articles, baseline, optimized, scores, random_edges,
        args.tail_bytes, args.probe_bytes, args.probe_targets
    )
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
