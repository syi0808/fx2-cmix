# 20 MB causal-order and residual-control feasibility

Date: 2026-08-02

## Corpus and protocol

- Source: official 100,000,000-byte `enwik8`, SHA-256
  `2b49720ec4d78c3c9fabaee6e4179a5e997302b3a70029f30f2d582218c024a8`.
- Selection: all complete non-redirect articles available in the enwik8 prefix,
  filtered through the repository's `new_article_order` until the next article
  would exceed 20 MiB.
- Test corpus: 1,992 complete articles, 20,946,242 bytes, SHA-256
  `1c1c459d77465ff4c41e21680d7affbd54dd3f709657135de4c888e942032b7f`.
- Predictor path: `cmix -r` with dictionary pretraining. This deliberately tests
  the full Predictor on fixed article boundaries but bypasses the production
  WRT/phda preprocessing transform.
- Decision threshold: an improvement of at least `0.001 bpb`, equivalent to
  2,618.3 bytes on this corpus. Stored-order cost is subtracted.

The raw-Predictor protocol makes this a bounded feasibility result, not a
submission-size result. Any positive ordering result must next be confirmed on
the production-preprocessed stream before a 100 MB expansion.

## Causal State-Transfer Ordering

The cheap probe warms an adaptive byte-trigram model on the last 16 KiB of an
article and scores the first 4 KiB of a successor. Optimization is restricted
to repeated adjacent swaps inside fixed 32-article blocks.

| Measurement | Result |
| --- | ---: |
| Articles moved | 1,171 / 1,992 |
| Proxy gain | 590,292.9 ideal bits |
| Full-Predictor edge targets | 12 |
| Full-Predictor directed edges | 36 |
| Within-target proxy/full Spearman | 0.741 |
| Optimized predecessor wins/losses | 6 / 4 |
| Random predecessor wins | 6 / 12 |
| Baseline payload | 3,439,608 bytes |
| Candidate payload | 3,436,750 bytes |
| Payload gain | 2,858 bytes (`0.001092 bpb`) |
| Compressed selected-order delta | +136 bytes |
| Net gain | 2,722 bytes (`0.001040 bpb`) |
| Candidate compression wall time | 4,466.12 seconds |
| Candidate raw roundtrip wall time | 3,557.59 seconds |
| Candidate peak RSS | 4,603,510,784 bytes |

The raw Predictor roundtrip is byte-identical. Both source and restored SHA-256
are `aefc3cf09b0ee0638312ea5beec8c0dbdb7042c197ad91a672d598b906c5ed6c`.

**Decision: conditional GO.** The net result clears the threshold by only 103.9
bytes and random predecessors win as often as optimized predecessors in the
small edge sample. Repeat this same 20 MB comparison through production
preprocessing before spending resources on 100 MB. Do not proceed directly to
a full enwik9 run.

## Residual Control Program Compiler

The R1 experiment records exact counterfactual loss for final-logit scales
`{0.75, 0.875, 1.0, 1.125, 1.25}` in one baseline pass. Features are quantized
final confidence and FXCM/ByteMixer disagreement. Article IDs are assigned to
fixed hash splits: 0-4 training, 5 tuning, 6 validation, and 7 frozen holdout.
Rules require at least 65,536 training bits and positive tuning and validation
gain. The frozen holdout is read only after compilation.

| Measurement | Result |
| --- | ---: |
| Compiled rules | 8 |
| Training gain | 722.5 bits |
| Tuning gain | 83.1 bits |
| Validation gain | 100.1 bits |
| Frozen holdout gain | 159.4 bits |
| Total counterfactual gain | 1,065.1 bits (133.1 bytes) |
| Total gain rate | `0.0000508 bpb` |
| Required gain | 20,946.2 bits (`0.001 bpb`) |
| Gzip-compressed binary delta | +200 bytes |

The no-op controller is payload-byte-identical to the pre-instrumentation
binary on the 50,051-byte smoke corpus. The generated R1 program is already net
negative after the gzip binary-size proxy, before an UPX/submission build.

**Decision: NO-GO for this R1 compiler.** The held-out gain has the right sign,
but is 19.7 times below the payload threshold and smaller than its compressed
code cost. Do not advance these confidence/disagreement final-logit rules to
R2/R3 or 100 MB. A materially different family-gating hypothesis would need a
new experiment rather than a larger search over these R1 cells.

## Reproduction artifacts

Generated corpora, traces, compressed payloads, and reports live under the
ignored `experiment-data/` directory. The committed entry points are:

- `experiments/common/prepare_article_corpus.py`
- `experiments/causal_order/run_order_experiment.py`
- `experiments/causal_order/run_full_probes.py`
- `experiments/causal_order/analyze_full_probes.py`
- `tools/control_compiler/compile_control_program.py`

Build the instrumented binary with
`CFLAGS_DEFINES="-DFX2_EXPERIMENT_TRACE=1 -DFX2_CONTROL_PROGRAM=1"` and set
`FX2_EXPERIMENT_TRACE_PATH` plus `FX2_ARTICLE_MANIFEST` for the aggregate pass.
Both compile-time flags default to zero in an ordinary build.

`cmix -q dictionary input.cmix output` decodes a `cmix -r` payload directly to
raw Predictor bytes; it is used for the fixed-boundary roundtrip check.
