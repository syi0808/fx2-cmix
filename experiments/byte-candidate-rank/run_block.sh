#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

CXX="${CXX:-clang++-17}"
PREDICTOR_INPUT="${1:-experiments/byte-candidate-rank/out/preprocess.out.cmix.temp}"
DICT="${2:-dictionary/english.dic}"
OUT_DIR="${FX2_BYTE_RANK_OUT_DIR:-experiments/byte-candidate-rank/out}"
mkdir -p "$OUT_DIR"

if [[ ! -s "$PREDICTOR_INPUT" ]]; then
  echo "missing predictor input: $PREDICTOR_INPUT" >&2
  exit 2
fi

"$CXX" -m64 -Wall -std=c++17 -O3 -ffp-model=fast -fno-exceptions \
  -fno-threadsafe-statics -march=native -mtune=native \
  -Wno-unused-variable -Wno-unused-but-set-variable -Wno-format \
  -Wno-unneeded-internal-declaration \
  -c experiments/byte-candidate-rank/block_rank.cpp -o ppmd-block-rank.o
"$CXX" -m64 -std=c++17 ppmd-block-rank.o ppmd.o byte-model.o \
  -o ppmd-block-rank

"$CXX" -m64 -Wall -std=c++17 -O3 -ffp-model=fast -fno-exceptions \
  -fno-threadsafe-statics -march=native -mtune=native \
  -Wno-unused-variable -Wno-unused-but-set-variable -Wno-format \
  -Wno-unneeded-internal-declaration \
  -c experiments/byte-candidate-rank/block_baseline.cpp -o full-block-baseline.o

BASELINE_OBJECTS=()
for object in *.o; do
  case "$object" in
    runner.o|byte-candidate-rank.o|ppmd-pair-rank.o|ppmd-block-rank.o|full-block-baseline.o)
      continue
      ;;
  esac
  BASELINE_OBJECTS+=("$object")
done
if [[ "$(uname -s)" == "Darwin" ]]; then
  LINK_GC=(-Wl,-dead_strip)
else
  LINK_GC=(-Wl,--gc-sections)
fi
"$CXX" -m64 "${LINK_GC[@]}" -std=c++17 full-block-baseline.o \
  "${BASELINE_OBJECTS[@]}" -o full-block-baseline

./ppmd-block-rank "$PREDICTOR_INPUT" \
  >"$OUT_DIR/block-rank.csv" 2>"$OUT_DIR/block-rank.stderr.log"
./full-block-baseline "$PREDICTOR_INPUT" "$DICT" \
  >"$OUT_DIR/block-baseline.csv" 2>"$OUT_DIR/block-baseline.stderr.log"

cat "$OUT_DIR/block-rank.csv"
cat "$OUT_DIR/block-baseline.csv"
