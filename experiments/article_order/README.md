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
command. The fixed-size enwik9 corpus ends during the next article; that
truncated trailing page is intentionally ignored, matching the C++ reorder
parser.

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

If the original embedding vectors are unavailable, the current semantic order
can be reused as a smooth position prior:

```bash
python3 experiments/article_order/extract_order_position_features.py \
  --features /data/article-order/structural.tsv \
  --order src/readalike_prepr/data/new_article_order \
  --output /data/article-order/current-position.tsv

python3 experiments/article_order/build_hierarchical_order.py \
  --block /data/article-order/current-position.tsv:0.5 \
  --block /data/article-order/structural.tsv:0.5 \
  --output /data/article-order/order.txt
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

Before committing to a multi-day cmix run, a cheap full-corpus locality proxy
can be produced without materializing the reordered XML:

```bash
python3 experiments/article_order/reorder_corpus.py \
  --input /data/enwik9 \
  --features /data/article-order/structural.tsv \
  --order src/readalike_prepr/data/new_article_order \
  | gzip -9 > /data/article-order/current-pages.xml.gz
```

## 4. Enable latent topic mixers

`LATENT_TOPIC_CONTEXT` is a bit mask for dedicated article-position mixers:

- `1`: coarse, 4096 articles
- `2`: mid, 512 articles
- `4`: fine, 64 articles

For example, build all three resolutions with:

```bash
make CFLAGS_DEFINES="-DSEED=923 -DUPDATE_LIMIT=3000 -DLATENT_TOPIC_CONTEXT=7"
```

For the PGO/self-extracting build, use:

```bash
LATENT_TOPIC_CONTEXT=7 ./build_and_construct_comp.sh
```

The article counter is updated from decoded `</page>` boundaries in FXCM, so
compression and decompression derive the same context without storing topic
metadata. Use `LATENT_TOPIC_CONTEXT=0` for the reorder-only ablation.
