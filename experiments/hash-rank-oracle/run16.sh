#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

if [[ "$(uname -s)" == "Darwin" ]]; then
  CXX="${CXX:-clang++}"
  LINK_FLAGS=( -m64 -Wl,-dead_strip -std=c++17 )
else
  CXX="${CXX:-clang++-17}"
  LINK_FLAGS=( -m64 -Wl,--gc-sections -std=c++17 )
fi

INPUT="${1:-prof_input/input2}"
DICT="${2:-dictionary/english.dic}"
OUT_DIR="${FX2_ORACLE_OUT_DIR:-experiments/hash-rank-oracle/out16-private}"
mkdir -p "$OUT_DIR"

# Exact counterfactual replay needs child PPMD writes to stay private.
python3 experiments/hash-rank-oracle/prepare_private_ppmd.py

make clean
make fast slow

REGULAR_OBJECTS=( *.o )
"$CXX" "${LINK_FLAGS[@]}" "${REGULAR_OBJECTS[@]}" -s -o cmix

"$CXX" -m64 -Wall -std=c++17 -O3 -ffp-model=fast -fno-exceptions \
  -fno-threadsafe-statics -march=native -mtune=native \
  -Wno-unused-variable -Wno-unused-but-set-variable -Wno-format \
  -Wno-unneeded-internal-declaration \
  -c experiments/hash-rank-oracle/hash_rank_oracle16_replay.cpp \
  -o hash_rank_oracle16_replay.o

ORACLE_OBJECTS=()
for object in *.o; do
  [[ "$object" == "runner.o" ]] && continue
  [[ "$object" == "hash_rank_oracle16_ascii.o" ]] && continue
  [[ "$object" == "hash_rank_oracle16.o" ]] && continue
  ORACLE_OBJECTS+=("$object")
done
"$CXX" "${LINK_FLAGS[@]}" "${ORACLE_OBJECTS[@]}" -o hash-rank-oracle16

rm -f "$OUT_DIR/preprocess.out" "$OUT_DIR/preprocess.out.cmix.temp"
FX2_PREPROCESS_ONLY=1 FX2_KEEP_TEMP=1 \
  ./cmix -c "$DICT" "$INPUT" "$OUT_DIR/preprocess.out" \
  >"$OUT_DIR/preprocess.stdout.log" 2>"$OUT_DIR/preprocess.stderr.log"

PREDICTOR_INPUT="$OUT_DIR/preprocess.out.cmix.temp"
if [[ ! -s "$PREDICTOR_INPUT" ]]; then
  echo "missing predictor input: $PREDICTOR_INPUT" >&2
  exit 2
fi

env \
  FX2_ORACLE16_POSITION="${FX2_ORACLE16_POSITION:-33822}" \
  FX2_ORACLE16_NODE_BUDGET="${FX2_ORACLE16_NODE_BUDGET:-50000}" \
  ./hash-rank-oracle16 "$PREDICTOR_INPUT" "$DICT" \
  >"$OUT_DIR/results16.csv" 2>"$OUT_DIR/oracle16.stderr.log"

cat "$OUT_DIR/results16.csv"
echo "--- oracle16 stderr ---"
cat "$OUT_DIR/oracle16.stderr.log"
