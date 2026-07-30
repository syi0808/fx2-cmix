#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INPUT="${1:?usage: bench_url_model.sh predictor-input [dictionary]}"
DICTIONARY="${2:-$ROOT/dictionary/english.dic}"
RESULT_DIR="${RESULT_DIR:-$ROOT/experiment-data/url-model}"
INPUT_LIMIT="${INPUT_LIMIT:-1048576}"
SEED="${SEED:-923}"
UPDATE_LIMIT="${UPDATE_LIMIT:-3000}"
COMPILER="${COMPILER:-clang++-17}"
MAKE_ARGS="${MAKE_ARGS:-}"
LFLAGS_OVERRIDE="${LFLAGS_OVERRIDE:-}"
VARIANTS="${VARIANTS:-baseline complete}"
PRETRAIN="${PRETRAIN:-1}"
REPEATS="${REPEATS:-3}"

mkdir -p "$RESULT_DIR"

definitions() {
  case "$1" in
    baseline) echo "-DURL_MODEL=0" ;;
    detector-only) echo "-DURL_MODEL=0" ;;
    complete|sidecar) echo "-DURL_MODEL=1 -DURL_INTEGRATED=0 -DURL_SIDECAR=1" ;;
    integrated) echo "-DURL_MODEL=1 -DURL_INTEGRATED=1 -DURL_SIDECAR=0" ;;
    with-match) echo "-DURL_MODEL=1 -DURL_MATCH_HEAD=1" ;;
    no-syntax) echo "-DURL_MODEL=1 -DURL_SYNTAX_HEAD=0" ;;
    no-component) echo "-DURL_MODEL=1 -DURL_COMPONENT_HEAD=0" ;;
    no-relation) echo "-DURL_MODEL=1 -DURL_RELATION_HEAD=0" ;;
    no-template) echo "-DURL_MODEL=1 -DURL_TEMPLATE_HEAD=0" ;;
    no-match) echo "-DURL_MODEL=1 -DURL_MATCH_HEAD=0" ;;
    no-residual) echo "-DURL_MODEL=1 -DURL_SIDECAR=0" ;;
    integrated-no-mixer) echo "-DURL_MODEL=1 -DURL_INTEGRATED=1 -DURL_SIDECAR=0 -DURL_ROLE_MIXER=0" ;;
    *) echo "unknown variant: $1" >&2; exit 2 ;;
  esac
}

printf 'variant\tpayload_bytes\tbinary_bytes\twall_seconds\tcpu_seconds\tpeak_rss_kb\n' \
  > "$RESULT_DIR/results.tsv"
extra_make_args=()
if [[ -n "$LFLAGS_OVERRIDE" ]]; then
  extra_make_args+=("LFLAGS=$LFLAGS_OVERRIDE")
fi

for variant in $VARIANTS; do
  make -C "$ROOT" clean >/dev/null
  make -C "$ROOT" cmix $MAKE_ARGS "${extra_make_args[@]}" CC="$COMPILER" \
    CFLAGS_DEFINES="-DSEED=$SEED -DUPDATE_LIMIT=$UPDATE_LIMIT $(definitions "$variant")"
  cp "$ROOT/cmix" "$RESULT_DIR/cmix-$variant"
  skip_pretrain=0
  if [[ "$PRETRAIN" == "0" ]]; then
    skip_pretrain=1
  fi
  wall_times=()
  cpu_times=()
  peak_rss_kb=0
  for run in $(seq 1 "$REPEATS"); do
    metrics="$RESULT_DIR/$variant.run-$run.time.txt"
    trace_path=""
    if [[ "$run" == "1" ]]; then
      trace_path="$RESULT_DIR/$variant.trace.csv"
    fi
    output="$RESULT_DIR/$variant.run-$run.cmix"
    start_ns="$(python3 -c 'import time; print(time.time_ns())')"
    if [[ "$(uname -s)" == "Darwin" ]]; then
      /usr/bin/time -l env \
        FX2_INPUT_LIMIT="$INPUT_LIMIT" \
        FX2_SKIP_PRETRAIN="$skip_pretrain" \
        FX2_URL_TRACE="$trace_path" \
        "$RESULT_DIR/cmix-$variant" -r "$DICTIONARY" "$INPUT" \
        "$output" 2>"$metrics"
      run_cpu="$(grep -Eo '[0-9.]+ real +[0-9.]+ user' "$metrics" |
        tail -1 | awk '{print $3}')"
      run_rss="$(awk \
        '/maximum resident set size/{print int($1 / 1024)}' "$metrics")"
    else
      /usr/bin/time -v env \
        FX2_INPUT_LIMIT="$INPUT_LIMIT" \
        FX2_SKIP_PRETRAIN="$skip_pretrain" \
        FX2_URL_TRACE="$trace_path" \
        "$RESULT_DIR/cmix-$variant" -r "$DICTIONARY" "$INPUT" \
        "$output" 2>"$metrics"
      run_cpu="$(awk -F: \
        '/User time \\(seconds\\)/{gsub(/ /, "", $2); print $2}' "$metrics")"
      run_rss="$(awk -F: \
        '/Maximum resident set size/{gsub(/ /, "", $2); print $2}' "$metrics")"
    fi
    end_ns="$(python3 -c 'import time; print(time.time_ns())')"
    wall_times+=("$(python3 -c \
      "print(($end_ns - $start_ns) / 1e9)")")
    cpu_times+=("$run_cpu")
    if (( (REPEATS == 1 || run > 1) && run_rss > peak_rss_kb )); then
      peak_rss_kb="$run_rss"
    fi
  done
  cp "$RESULT_DIR/$variant.run-1.cmix" "$RESULT_DIR/$variant.cmix"
  wall_median="$(python3 -c \
    'import statistics,sys; print(statistics.median(map(float,sys.argv[1:])))' \
    "${wall_times[@]}")"
  cpu_median="$(python3 -c \
    'import statistics,sys; print(statistics.median(map(float,sys.argv[1:])))' \
    "${cpu_times[@]}")"
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$variant" \
    "$(wc -c < "$RESULT_DIR/$variant.cmix" | tr -d ' ')" \
    "$(wc -c < "$RESULT_DIR/cmix-$variant" | tr -d ' ')" \
    "$wall_median" "$cpu_median" "$peak_rss_kb" \
    >> "$RESULT_DIR/results.tsv"
done

if [[ -f "$RESULT_DIR/baseline.trace.csv" &&
      -f "$RESULT_DIR/complete.trace.csv" ]]; then
  "$ROOT/tools/analyze_url_trace.py" \
    "$RESULT_DIR/baseline.trace.csv" "$RESULT_DIR/complete.trace.csv" \
    | tee "$RESULT_DIR/trace-comparison.txt"
fi
