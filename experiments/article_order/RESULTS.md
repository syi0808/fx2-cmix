# Article-order experiment results

## 2026-07-29 structural pass

Corpus:

- enwik9 size: 1,000,000,000 bytes
- SHA-256: `159b85351e5f76e60cbe32e04c677847a9ecba3adc79addab6f4c6c7aa3744bc`
- complete articles: 243,425
- non-redirect articles: 172,315

Feature extraction took 47.55 seconds. The final truncated XML page was ignored,
matching the C++ reorder parser.

### Structural cohesion

Lower is better.

| Order | Adjacent distance | Within 64 MSE | Within 512 MSE | Within 4096 MSE |
| --- | ---: | ---: | ---: | ---: |
| Current | 0.9130 | 0.5518 | 0.6266 | 0.7014 |
| Structural only | 0.8217 | 0.4111 | 0.4856 | 0.5648 |
| Current-position 70% + structural 30% | 0.8452 | 0.4453 | 0.5557 | 0.6802 |

### Stored-order cost

The order files were compressed by the same non-PGO x86_64 binary under
Rosetta. These values are suitable for relative comparison, not submission
size reporting.

| Order | Raw bytes | Compressed bytes | Delta |
| --- | ---: | ---: | ---: |
| Current | 1,094,862 | 201,031 | 0 |
| Structural only | 1,095,095 | 234,172 | +33,141 |
| Current-position 70% + structural 30% | 1,095,095 | 216,920 | +15,889 |

### Full-page gzip proxy

All 243,425 complete pages were streamed in each order and compressed with
`gzip -9`.

| Order | Compressed bytes | Delta |
| --- | ---: | ---: |
| Current | 305,737,031 | 0 |
| Current-position only | 306,296,214 | +559,183 |
| Current-position 90% + structural 10% | 307,561,737 | +1,824,706 |
| Current-position 70% + structural 30% | 309,528,280 | +3,791,249 |

The structural candidates improve their target feature cohesion but lose the
semantic and manual locality already encoded in the current permutation. They
are rejected before the multi-day cmix body benchmark. The next experiment
keeps the current article order and measures latent topic mixers independently.

## 2026-07-29 bounded latent-topic pass

The mini corpus contains four 512-article slices at current-order ranks 0,
40,000, 80,000, and 120,000. Each article retains its title and first 512 text
bytes. This produces 2,048 articles and 1,303,221 bytes. A shuffled control uses
the identical articles and shuffle seed 923.

One `-n` compression pass evaluated five independent adaptive top-mixer
branches before SSE. Topic shifts 9/6/3 represent 512/64/8-article blocks.
Negative deltas are better.

| Input | Mask 0 bytes | Coarse (1) delta | Mid (2) delta | Fine (4) delta | All (7) delta |
| --- | ---: | ---: | ---: | ---: | ---: |
| Current order | 131,900.932 | -0.181 | -7.910 | -6.945 | -16.729 |
| Shuffled | 138,198.239 | -13.601 | -26.187 | -11.621 | -41.470 |

The actual mask-0 compressed streams were 131,784 bytes in current order and
137,992 bytes shuffled. Thus current ordering saves 6,208 bytes (4.50%) on this
sample, while all latent mixers improve the ordered pre-SSE estimate by only
16.729 bytes (0.0127%). The larger topic-mixer gain after shuffling shows that
the small benefit is compatible with arbitrary block-specific adaptation, not
evidence that the index prefixes recover useful topics.

An actual mask-7 smoke compression reduced a 48,617-byte corpus from 7,937 to
7,923 bytes, and its decompressed SHA-256 matched the source. This validates
the counter and codec symmetry but not full-corpus profitability.

Conclusion: the existing semantic/manual ordering is strongly useful, but
adding index-derived topic mixers is not justified by this bounded test. Its
measured gain is too small to cover extra code and memory confidently. The
result is directional rather than a proof because pages are truncated, topic
blocks are scaled down, preprocessing is disabled, and shadow loss excludes
SSE.

The latent-topic and shadow-evaluation paths were removed from the production
compressor after this rejection. The mini-corpus generator and these results
remain only for reproducibility.
