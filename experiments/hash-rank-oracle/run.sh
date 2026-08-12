#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

CXX="${CXX:-clang++-17}"
INPUT="${1:-prof_input/input2}"
DICT="${2:-dictionary/english.dic}"
OUT_DIR="${FX2_ORACLE_OUT_DIR:-experiments/hash-rank-oracle/out}"
mkdir -p "$OUT_DIR"

# Build the production predictor and the oracle from exactly the same objects.
make clean
make fast slow

REGULAR_OBJECTS=( *.o )
"$CXX" -m64 -Wl,--gc-sections -std=c++17 "${REGULAR_OBJECTS[@]}" -s -o cmix

"$CXX" -m64 -Wall -std=c++17 -O3 -ffp-model=fast -fno-exceptions \
  -fno-threadsafe-statics -march=native -mtune=native \
  -Wno-unused-variable -Wno-unused-but-set-variable -Wno-format \
  -c experiments/hash-rank-oracle/hash_rank_oracle.cpp \
  -o hash_rank_oracle.o

ORACLE_OBJECTS=()
for object in *.o; do
  [[ "$object" == "runner.o" ]] && continue
  ORACLE_OBJECTS+=("$object")
done
"$CXX" -m64 -Wl,--gc-sections -std=c++17 "${ORACLE_OBJECTS[@]}" \
  -o hash-rank-oracle

# Produce the same dictionary-preprocessed stream that RunCompression feeds to
# Predictor, but stop before arithmetic coding.
rm -f "$OUT_DIR/preprocess.out" "$OUT_DIR/preprocess.out.cmix.temp"
FX2_PREPROCESS_ONLY=1 FX2_KEEP_TEMP=1 \
  ./cmix -c "$DICT" "$INPUT" "$OUT_DIR/preprocess.out" \
  >"$OUT_DIR/preprocess.stdout.log" 2>"$OUT_DIR/preprocess.stderr.log"

PREDICTOR_INPUT="$OUT_DIR/preprocess.out.cmix.temp"
if [[ ! -s "$PREDICTOR_INPUT" ]]; then
  echo "missing predictor input: $PREDICTOR_INPUT" >&2
  exit 2
fi

/usr/bin/time -v env \
  FX2_ORACLE_WARMUP="${FX2_ORACLE_WARMUP:-32768}" \
  FX2_ORACLE_SAMPLES="${FX2_ORACLE_SAMPLES:-8}" \
  FX2_ORACLE_STRIDE="${FX2_ORACLE_STRIDE:-65536}" \
  ./hash-rank-oracle "$PREDICTOR_INPUT" "$DICT" \
  >"$OUT_DIR/results.csv" 2>"$OUT_DIR/oracle.stderr.log"

cat "$OUT_DIR/results.csv"
echo "--- oracle stderr ---"
cat "$OUT_DIR/oracle.stderr.log"
