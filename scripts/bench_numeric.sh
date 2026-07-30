#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RESULT_DIR="${RESULT_DIR:-$ROOT/experiment-data/numeric}"
INPUT="${1:-$RESULT_DIR/numeric-fixture.txt}"
SEED="${SEED:-923}"
UPDATE_LIMIT="${UPDATE_LIMIT:-3000}"
MAKE_ARGS="${MAKE_ARGS:-}"
COMPILER="${COMPILER:-clang++-17}"

mkdir -p "$RESULT_DIR"

if [[ ! -f "$INPUT" ]]; then
  {
    seq 1900 2099 | paste -sd' ' -
    printf '%s\n' \
      '2006-03-03T12:34:56Z 2006-03-04T12:34:57Z' \
      '<id>123456</id> <id>123457</id> <id>123458</id>' \
      '1.0.0 1.0.1 1.0.2 3.1415926535 1.23e-10' \
      '5839102746 0192837465 9182736450'
  } > "$INPUT"
fi

md5_file() {
  if command -v md5sum >/dev/null 2>&1; then
    md5sum "$1" | awk '{print $1}'
  else
    md5 -q "$1"
  fi
}

build_variant() {
  local name="$1"
  local enabled="$2"
  make -C "$ROOT" clean
  make -C "$ROOT" cmix $MAKE_ARGS CC="$COMPILER" \
    CFLAGS_DEFINES="-DSEED=$SEED -DUPDATE_LIMIT=$UPDATE_LIMIT -DNUMERIC_LOCAL_MODEL=$enabled"
  cp "$ROOT/cmix" "$RESULT_DIR/cmix-$name"
}

run_variant() {
  local name="$1"
  local binary="$RESULT_DIR/cmix-$name"
  local archive="$RESULT_DIR/$name.cmix"
  local restored="$RESULT_DIR/$name.restored"
  local trace="$RESULT_DIR/$name.trace.json"
  local start end elapsed max_rss time_log
  time_log="$RESULT_DIR/$name.time.log"

  start="$(date +%s)"
  if /usr/bin/time -v true >/dev/null 2>&1; then
    /usr/bin/time -v env FX2_NUMERIC_TRACE="$trace" \
      "$binary" -n "$INPUT" "$archive" 2> "$time_log"
    max_rss="$(awk -F: '/Maximum resident set size/ {gsub(/ /, "", $2); print $2}' "$time_log")"
  else
    /usr/bin/time -l env FX2_NUMERIC_TRACE="$trace" \
      "$binary" -n "$INPUT" "$archive" 2> "$time_log"
    max_rss="$(awk '/maximum resident set size/ {print $1}' "$time_log")"
  fi
  end="$(date +%s)"
  elapsed="$((end - start))"
  "$binary" -d "$archive" "$restored"
  cmp "$INPUT" "$restored"

  printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$name" "$(wc -c < "$archive" | tr -d ' ')" "$elapsed" \
    "$max_rss" "$(wc -c < "$binary" | tr -d ' ')" "$(md5_file "$restored")" \
    >> "$RESULT_DIR/results.tsv"
}

{
  printf 'git_commit\t%s\n' "$(git -C "$ROOT" rev-parse HEAD)"
  printf 'seed\t%s\n' "$SEED"
  printf 'update_limit\t%s\n' "$UPDATE_LIMIT"
  printf 'input\t%s\n' "$INPUT"
  printf 'input_bytes\t%s\n' "$(wc -c < "$INPUT" | tr -d ' ')"
  printf 'make_args\t%s\n' "$MAKE_ARGS"
  printf 'compiler\t%s\n' "$("$COMPILER" --version | head -1)"
} > "$RESULT_DIR/manifest.tsv"

printf 'variant\tpayload_bytes\tseconds\tmax_rss\tbinary_bytes\trestored_md5\n' \
  > "$RESULT_DIR/results.tsv"

build_variant baseline 0
run_variant baseline
FX2_NUMERIC_TRACE= "$RESULT_DIR/cmix-baseline" -n \
  "$INPUT" "$RESULT_DIR/baseline-repeat.cmix"
cmp "$RESULT_DIR/baseline.cmix" "$RESULT_DIR/baseline-repeat.cmix"
build_variant numeric-local 1
run_variant numeric-local

"$ROOT/tools/analyze_numeric_trace.py" \
  "$RESULT_DIR/baseline.trace.json" \
  "$RESULT_DIR/numeric-local.trace.json" \
  | tee "$RESULT_DIR/trace-comparison.txt"
