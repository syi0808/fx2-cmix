#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INPUT="${1:?usage: bench_numeric_structure.sh predictor-input [dictionary]}"
DICTIONARY="${2:-$ROOT/dictionary/english.dic}"
RESULT_DIR="${RESULT_DIR:-$ROOT/experiment-data/numeric-structure}"
INPUT_LIMIT="${INPUT_LIMIT:-1048576}"
SEED="${SEED:-923}"
UPDATE_LIMIT="${UPDATE_LIMIT:-3000}"
COMPILER="${COMPILER:-clang++-17}"
MAKE_ARGS="${MAKE_ARGS:-}"
LFLAGS_OVERRIDE="${LFLAGS_OVERRIDE:-}"
VARIANTS="${VARIANTS:-B0 B1 B2 B2M L1 L2 S1 F1}"
PRETRAIN="${PRETRAIN:-1}"

mkdir -p "$RESULT_DIR"

definitions() {
  case "$1" in
    B0) echo "" ;;
    B1) echo "-DNUMERIC_BOUNDARY_MODEL=1" ;;
    B2) echo "-DNUMERIC_BOUNDARY_MODEL=1 -DNUMERIC_BOUNDARY_SEMANTIC=1" ;;
    B2M) echo "-DNUMERIC_BOUNDARY_MODEL=1 -DNUMERIC_BOUNDARY_SEMANTIC=1 -DNUMERIC_BOUNDARY_MIXER=1" ;;
    L1) echo "-DNUMERIC_BOUNDARY_MODEL=1 -DNUMERIC_LINKED_MODEL=1" ;;
    L2) echo "-DNUMERIC_BOUNDARY_MODEL=1 -DNUMERIC_LINKED_MODEL=1 -DNUMERIC_LINKED_SHAPE=1" ;;
    S1) echo "-DNUMERIC_START_MODEL=1" ;;
    F1) echo "-DNUMERIC_BOUNDARY_MODEL=1 -DNUMERIC_BOUNDARY_SEMANTIC=1 -DNUMERIC_LINKED_MODEL=1 -DNUMERIC_LINKED_SHAPE=1 -DNUMERIC_START_MODEL=1 -DNUMERIC_FIELD_MODEL=1" ;;
    *) echo "unknown variant: $1" >&2; exit 2 ;;
  esac
}

printf 'variant\tpayload_bytes\tideal_bits\tseconds\n' > "$RESULT_DIR/results.tsv"
extra_make_args=()
if [[ -n "$LFLAGS_OVERRIDE" ]]; then
  extra_make_args+=("LFLAGS=$LFLAGS_OVERRIDE")
fi
for variant in $VARIANTS; do
  make -C "$ROOT" clean >/dev/null
  make -C "$ROOT" cmix $MAKE_ARGS "${extra_make_args[@]}" CC="$COMPILER" \
    CFLAGS_DEFINES="-DSEED=$SEED -DUPDATE_LIMIT=$UPDATE_LIMIT $(definitions "$variant")"
  cp "$ROOT/cmix" "$RESULT_DIR/cmix-$variant"
  start="$(date +%s)"
  skip_pretrain=0
  if [[ "$PRETRAIN" == "0" ]]; then
    skip_pretrain=1
  fi
  FX2_INPUT_LIMIT="$INPUT_LIMIT" \
  FX2_SKIP_PRETRAIN="$skip_pretrain" \
  FX2_NUMERIC_TRACE="$RESULT_DIR/$variant.trace.json" \
  FX2_NUMERIC_PROB_TRACE="$RESULT_DIR/$variant.prob.bin" \
    "$RESULT_DIR/cmix-$variant" -r "$DICTIONARY" "$INPUT" \
    "$RESULT_DIR/$variant.cmix"
  end="$(date +%s)"
  ideal_bits="$("$COMPILER" --version >/dev/null 2>&1; python3 -c \
    'import json,sys; print(json.load(open(sys.argv[1]))["events"]["all"]["bits"])' \
    "$RESULT_DIR/$variant.trace.json")"
  printf '%s\t%s\t%s\t%s\n' "$variant" \
    "$(wc -c < "$RESULT_DIR/$variant.cmix" | tr -d ' ')" \
    "$ideal_bits" "$((end - start))" >> "$RESULT_DIR/results.tsv"
done

traces=()
for variant in $VARIANTS; do
  traces+=("$RESULT_DIR/$variant.trace.json")
done
"$ROOT/tools/analyze_numeric_trace.py" "${traces[@]}" \
  | tee "$RESULT_DIR/trace-comparison.txt"

if [[ -f "$RESULT_DIR/B0.prob.bin" && -f "$RESULT_DIR/B2.prob.bin" ]]; then
  "$ROOT/tools/analyze_numeric_trace.py" \
    "$RESULT_DIR/B0.trace.json" "$RESULT_DIR/B2.trace.json" \
    --probability-traces \
      "$RESULT_DIR/B0.prob.bin" "$RESULT_DIR/B2.prob.bin" \
    --warmup-bytes "${WARMUP_BYTES:-0}" \
    | tee "$RESULT_DIR/boundary-alpha-sweep.txt"
fi
