# Numeric structure experiment results

## Predictor stream audit

The initial audit used the 686,397-byte
`experiment-data/article-order/schema-sample.xml` corpus. Dictionary
preprocessing produced a 463,493-byte Predictor stream containing:

| Metric | Count |
| --- | ---: |
| Numeric runs | 2,318 |
| Digit bytes | 7,359 |
| `DDDD-DD-DD` sequences | 129 |
| `DD:DD:DD` sequences | 128 |
| Dash separators | 275 |
| Colon separators | 282 |
| Dot separators | 140 |

The most common run lengths were 2 (912 runs), 4 (472), 3 (263), and 1
(258). This confirms that linked date/time-like structure survives dictionary
preprocessing in this sample.

## 20 KB smoke benchmark

The first B0/B1/B2 smoke benchmark used the first 20,000 bytes of that
Predictor stream. Dictionary pretraining was intentionally skipped to keep the
correctness run short.

| Variant | Payload bytes | Ideal bits | Delta vs B0 |
| --- | ---: | ---: | ---: |
| B0 | 6,077 | 48,316.238 | — |
| B1 | 6,077 | 48,316.221 | -0.018 bits |
| B2 | 6,077 | 48,316.167 | -0.071 bits |

B1's standalone boundary loss fell from 8.0 to 7.079 bits per ended run.
B2's standalone boundary loss fell to 6.379 bits per ended run, while final
mixed loss improved by only 0.071 bits and payload size did not change.

This is a correctness-scale result, not evidence for a full enwik9 run. It
shows that the boundary context learns the target event, but the sample is too
small for the Mixer or arithmetic payload to realize a measurable gain. The
next decision point remains a pre-trained 1 MB run followed by 10 MB and 50 MB
only if payload and non-target loss move in the same direction.
