#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

CXX="${CXX:-clang++-17}"
PREDICTOR_INPUT="${1:-experiments/byte-candidate-rank/out/preprocess.out.cmix.temp}"
OUT_DIR="${FX2_BYTE_RANK_OUT_DIR:-experiments/byte-candidate-rank/out}"
mkdir -p "$OUT_DIR"

if [[ ! -s "$PREDICTOR_INPUT" ]]; then
  echo "missing predictor input: $PREDICTOR_INPUT" >&2
  exit 2
fi

# run.sh has already patched PPMD to a stable MAP_PRIVATE arena and built the
# production objects. The pair oracle only needs PPMD + ByteModel.
"$CXX" -m64 -Wall -std=c++17 -O3 -ffp-model=fast -fno-exceptions \
  -fno-threadsafe-statics -march=native -mtune=native \
  -Wno-unused-variable -Wno-unused-but-set-variable -Wno-format \
  -Wno-unneeded-internal-declaration \
  -c experiments/byte-candidate-rank/pair_rank.cpp -o ppmd-pair-rank.o

"$CXX" -m64 -std=c++17 ppmd-pair-rank.o ppmd.o byte-model.o \
  -o ppmd-pair-rank

./ppmd-pair-rank "$PREDICTOR_INPUT" \
  >"$OUT_DIR/pair-results.csv" 2>"$OUT_DIR/pair.stderr.log"

cat "$OUT_DIR/pair-results.csv"
