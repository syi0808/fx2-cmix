# MediaWiki schema-transform experiment

## Scope

The enwik9 XML export uses the old `export-0.3` schema. A full scan found:

| Field | Occurrences |
| --- | ---: |
| `minor` | 98,817 |
| `username` | 205,397 |
| `ip` | 38,028 |
| `parentid` | 0 |
| `model` | 0 |
| `format` | 0 |
| `sha1` | 0 |

The tested transform therefore targeted fields that exist in this corpus:

- independent delta states for page, revision, contributor, and parent IDs
- implicit revision and contributor XML wrappers
- one-byte contributor username/IP variants
- compact timestamp and minor/model/format markers

The existing phda9 format already moves page metadata to a separate tail
stream and removes its indentation and most tag syntax. This experiment made
that internal stream more record-like.

## Reproduction

Build the standalone reversible-transform tool:

```bash
clang++ -std=c++17 -O2 -DPHDA9_SCHEMA_RECORD=1 \
  experiments/schema_transform/phda9_tool.cpp \
  -o /data/article-order/phda9-tool
```

Create the exact 128-page sample used below:

```bash
python3 experiments/article_order/build_mini_corpus.py \
  --input enwik9 \
  --features /data/article-order/structural.tsv \
  --order src/readalike_prepr/data/new_article_order \
  --output /data/article-order/schema-sample.xml \
  --segment 40000:128 \
  --preserve-pages
```

Encode, decode, and require a byte-identical result:

```bash
/data/article-order/phda9-tool encode schema-sample.xml sample.phda
/data/article-order/phda9-tool decode sample.phda sample.restored.xml
cmp schema-sample.xml sample.restored.xml
```

`schema_fixture.xml` additionally covers parent IDs, username/IP variants,
minor flags, model/format constants, and pass-through SHA-1 fields.

## Result

The input contained 128 current-order articles and 686,397 bytes. Both the
baseline and candidate transforms restored SHA-256
`b2645f1e20b993f3308b4f1ebbaf0e585dd8cffcf8b2c7625e11957fed48c99d`.

| Measurement | Baseline | Candidate | Delta |
| --- | ---: | ---: | ---: |
| phda9 output | 653,759 | 646,111 | -7,648 (-1.1699%) |
| `gzip -9` proxy | 232,016 | 231,719 | -297 (-0.1280%) |
| fx2-cmix `-n` output | 130,681 | 130,733 | +52 (+0.0398%) |

The record format removes raw bytes but slightly worsens the actual model's
compressed size. Repeated XML field names are already cheap and provide useful
predictor structure; revision and contributor deltas are also irregular after
article reordering. The candidate production transform was therefore rejected
and phda9 remains unchanged by default. `PHDA9_SCHEMA_RECORD` defaults to `0`;
the disabled implementation remains only to reproduce the experiment.
