# Article-order experiments

These scripts replace the exploratory notebook pipeline with deterministic,
fixed-boundary ordering experiments. Generated artifacts should stay outside
the repository.

## 1. Extract structural features

```bash
python3 experiments/article_order/extract_structural_features.py \
  /data/enwik9 /data/article-order/structural.tsv
```

The extractor follows the redirect rules used by
`src/readalike_prepr/article_remap.cpp`. Its `article_index` is the zero-based
position in the original XML and is compatible with the existing `remap`
command.

## 2. Build an order

Structural-only:

```bash
python3 experiments/article_order/build_hierarchical_order.py \
  --block /data/article-order/structural.tsv:1.0 \
  --output /data/article-order/order.txt
```

Existing Voyage embeddings plus structural features:

```bash
python3 experiments/article_order/build_hierarchical_order.py \
  --block /data/article-order/embeddings-4k.tsv:0.3:-1 \
  --block /data/article-order/structural.tsv:0.3 \
  --output /data/article-order/order.txt
```

The `-1` offset converts IDs produced by the existing embedding notebook to
zero-based article positions. Each feature block is standardized, reduced to
its highest-variance dimensions, row-normalized, and multiplied by its block
weight. Balanced two-means splits create exact 4096/512/64 boundaries; original
article order is retained inside each 64-article leaf.

Convert the full article positions to the redirect-skipping format embedded in
the compressor:

```bash
make remap
./remap /data/article-order/order.txt /data/enwik9 \
  > src/readalike_prepr/data/new_article_order
```

## 3. Record a result

```bash
python3 experiments/article_order/record_result.py \
  --name semantic-structural-topic-off \
  --csv /data/article-order/results.csv \
  --order src/readalike_prepr/data/new_article_order \
  --compressed-order run/comp_order \
  --body run/enwik9.comp \
  --compressor run/cmix \
  --archive run/archive9 \
  --original /data/enwik9 \
  --restored run/enwik9_restored
```

Always compare `total_submission_bytes`, not only the compressed body. A full
candidate is valid only when `restored_sha256_matches` is true.
