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

## Sidecar residual follow-up

The follow-up moves URL combination after the existing SSE and updates it only
for confirmed path segments, extensions, query keys and query values. URL
heads are no longer layer-0 inputs, the URL match is disabled by default, and
its match-length state is separate from the baseline match mixer.

On the same 262,149-byte URL diagnostic, the default four-head sidecar changed
payload from 57,839 to 57,817 bytes. Trace gain was 179.128 bits over 216,970
confirmed URL bytes (0.000826 bpb). Outside and scheme-candidate trace deltas
were exactly zero. Both baseline and sidecar Mach-O files were 332,712 bytes.

Complete-minus-one payload sizes were:

| variant | payload bytes | raw binary bytes |
| --- | ---: | ---: |
| sidecar with match | 57,816 | 349,304 |
| minus syntax | 57,819 | 332,728 |
| minus component | 57,822 | 332,728 |
| minus relation | 57,821 | 332,728 |
| minus template | 57,822 | 332,728 |
| minus match | 57,817 | 332,712 |

Match saved one additional payload byte but crossed a 16KB Mach-O page
boundary, so it is disabled in the default sidecar.

## Mixed-density smoke

A contiguous 524,288-byte `enwik9` slice produced a 317,459-byte Predictor
stream containing 3,565 confirmed URL bytes (1.12%). Results were:

| variant | payload bytes | raw binary bytes | total trace gain |
| --- | ---: | ---: | ---: |
| baseline | 93,870 | 332,712 | — |
| sidecar | 93,869 | 332,712 | 8.148 bits |
| integrated | 93,869 | 332,776 | 4.507 bits |

The integrated model gained 5.548 URL bits but lost 1.041 bits in candidate
and outside regions. The sidecar changed only confirmed URL roles, so its
outside and candidate deltas were exactly zero.

Three alternating runs gave median wall times of 53.71 seconds for baseline
and 54.09 seconds for sidecar (+0.71%). Median user CPU times were 45.39 and
45.38 seconds respectively. A 36,104-byte URL-free Predictor stream produced
identical 12,978-byte payloads and exactly zero trace delta.

These are correctness and direction checks, not acceptance evidence. The
sidecar fixes isolation and packaging overhead, but its measured gain remains
far below the 10KB full-score retention threshold. A 10–50MB mixed run is the
next decision point.
