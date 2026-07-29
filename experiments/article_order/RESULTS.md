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
