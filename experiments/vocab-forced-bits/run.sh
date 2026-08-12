#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

CXX="${CXX:-clang++}"
INPUT="${1:-prof_input/input}"
DICT="${2:-dictionary/english.dic}"
OUT_DIR="${FX2_VOCAB_OUT_DIR:-experiments/vocab-forced-bits/out}"
mkdir -p "$OUT_DIR"

make clean
make fast slow

REGULAR_OBJECTS=( *.o )
if [[ "$(uname -s)" == "Darwin" ]]; then
  LINK_GC=(-Wl,-dead_strip)
else
  LINK_GC=(-Wl,--gc-sections)
fi
"$CXX" -m64 "${LINK_GC[@]}" -std=c++17 "${REGULAR_OBJECTS[@]}" -o cmix

"$CXX" -m64 -Wall -std=c++17 -O3 -ffp-model=fast -fno-exceptions \
  -fno-threadsafe-statics -march=native -mtune=native \
  -Wno-unused-variable -Wno-unused-but-set-variable -Wno-format \
  -Wno-unneeded-internal-declaration \
  -c experiments/vocab-forced-bits/measure.cpp -o vocab-forced-measure.o

MEASURE_OBJECTS=()
for object in *.o; do
  [[ "$object" == "runner.o" ]] && continue
  MEASURE_OBJECTS+=("$object")
done
"$CXX" -m64 "${LINK_GC[@]}" -std=c++17 "${MEASURE_OBJECTS[@]}" \
  -o vocab-forced-measure

rm -f "$OUT_DIR/preprocess.out" "$OUT_DIR/preprocess.out.cmix.temp"
FX2_PREPROCESS_ONLY=1 FX2_KEEP_TEMP=1 \
  ./cmix -c "$DICT" "$INPUT" "$OUT_DIR/preprocess.out" \
  >"$OUT_DIR/preprocess.stdout.log" 2>"$OUT_DIR/preprocess.stderr.log"

PREDICTOR_INPUT="$OUT_DIR/preprocess.out.cmix.temp"
if [[ ! -s "$PREDICTOR_INPUT" ]]; then
  echo "missing predictor input: $PREDICTOR_INPUT" >&2
  exit 2
fi

./vocab-forced-measure "$PREDICTOR_INPUT" "$DICT" \
  >"$OUT_DIR/results.txt" 2>"$OUT_DIR/measure.stderr.log"
cat "$OUT_DIR/results.txt"
