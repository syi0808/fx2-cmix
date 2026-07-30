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

## Residual diagnostics

The raw terminator-byte entropy on the 463,493-byte audit stream was:

| Distribution | Entropy |
| --- | ---: |
| `H(T)` | 3.565 bits |
| `H(T | run length)` | 2.441 bits |
| `H(T | run length, start structure)` | 1.302 bits |
| `H(T | shape prefix)` | 1.585 bits |

These are in-sample empirical bounds, but they confirm that run length and
coarse start structure contain substantial terminator information.

A 100,000-byte no-pretrain diagnostic produced:

| Variant | Ideal bits | Active delta | Inactive delta | Payload |
| --- | ---: | ---: | ---: | ---: |
| B0 | 207,386.437 | — | — | 25,961 bytes |
| B2 | 207,385.735 | -0.603 bits | -0.099 bits | 25,960 bytes |
| B2M | 207,384.086 | -1.418 bits | -0.933 bits | 25,960 bytes |

B0 already encoded boundary-active bytes at 2.403 bits/byte, while the B2
standalone model used 5.844 bits/byte. The standalone model is therefore not a
replacement for the baseline Predictor.

The bit-level offline blend still found residual information. After a
20,000-byte warm-up, `alpha=0.25` improved boundary-active loss by 5.286 bits
over `alpha=0`; larger weights regressed. B2M uses a conditional 64-context
first-layer Mixer and improved the 100 KB active region, but the gain was only
0.00067 bits per active byte. This remains below the 0.02 bits/run promotion
threshold.

## Pre-trained 906 KB diagnostic

The larger diagnostic used a 906,802-byte dictionary-preprocessed stream made
from `mini-ordered.xml`, with normal dictionary pretraining enabled. It
contained 22,133 numeric runs and 55,347 digit bytes. Its 6.10% digit ratio and
Wiki-link-heavy shapes make it a diagnostic corpus rather than an enwik9
representative sample.

| Variant | Ideal bits | Active delta | Inactive delta | Payload |
| --- | ---: | ---: | ---: | ---: |
| B0 | 920,978.661 | — | — | 115,160 bytes |
| B2 | 920,963.988 | -14.255 bits | -0.417 bits | 115,158 bytes |
| B2M | 920,964.908 | -10.926 bits | -2.826 bits | 115,158 bytes |

The decisive residual comparison was:

| Region | B0 final | B2 standalone | B2 final |
| --- | ---: | ---: | ---: |
| Boundary active | 0.918340 | 2.552769 | 0.918082 |
| Continued | 1.276943 | 3.225817 | 1.276355 |
| Ended | 0.380200 | 1.542755 | 0.380438 |

B2 standalone remained much worse than the existing Predictor. B2's final
gain came entirely from continuation (-19.537 bits); ended runs regressed by
5.282 bits. B2M did not improve this tradeoff and was 0.920 bits worse than
B2 overall.

After a 200,000-byte warm-up, every tested positive offline blend weight
increased total boundary-active loss. The smallest weight, `alpha=0.03125`,
regressed by 7.641 bits. The positive residual seen at 100 KB therefore did not
survive the larger pre-trained run.

Although B2 saved two payload bytes, its active improvement was only 0.000258
bits per active byte and its total ideal gain was 14.673 bits. It fails the
0.02 bits/run promotion threshold by a wide margin. The result does not
justify a 10 MB or 50 MB run, field hashes, or more shape specialization.
