# URL model results

## 262 KB URL diagnostic

The measured corpus is the first 262,144 bytes of URL tokens extracted from
`enwik9`, then passed through the normal preprocessor. This is deliberately
URL-heavy and is useful for detector/model diagnostics, but it is not a
substitute for a full `enwik9` acceptance run.

Both variants used seed 923, update limit 3000, no dictionary pretraining, and
the same 262,149-byte Predictor stream on Apple arm64. `baseline` used
`URL_MODEL=0`; `complete` enabled every URL head and the role mixer.

| variant | payload bytes | wall seconds | binary bytes |
| --- | ---: | ---: | ---: |
| baseline | 57,839 | 35 | 332,744 |
| complete | 57,753 | 34 | 332,824 |

The complete model saved 86 payload bytes while adding 80 binary bytes. Trace
loss over 232,454 URL/candidate bytes changed from 437,771.234 to 437,059.480
bits, an improvement of 711.755 bits or 0.00306 bpb. Outside loss regressed by
0.00094 bpb, below the 0.002 bpb guardrail.

An 8 KB initialization-scale run measured peak RSS at 2,010,192 KB for the
baseline and 2,018,720 KB for the complete model, an 8,528 KB increase. The
small-corpus wall time is noisy; the 262 KB runs showed no measurable slowdown
(35 versus 34 seconds), while the raw-URL repeat measured 34 versus 36 seconds.

Role gains were concentrated in path segments (399.0 bits), domains
(117.6 bits), query keys (94.9 bits), query values (45.9 bits), and extensions
(43.8 bits). Scheme prediction was already nearly saturated by the baseline.

This smoke result passes round-trip and non-URL isolation checks, but it does
not meet the proposed 0.10 bpb URL gain or projected net-size targets. A full
corpus run and ablations are still required before treating the model as a
size win.
