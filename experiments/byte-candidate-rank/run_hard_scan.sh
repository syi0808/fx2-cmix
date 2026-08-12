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
  -c experiments/byte-candidate-rank/hard_scan.cpp -o hard-scan.o

SCAN_OBJECTS=()
for object in *.o; do
  case "$object" in
    runner.o|byte-candidate-rank.o|ppmd-pair-rank.o|ppmd-block-rank.o|full-block-baseline.o|hard-scan.o)
      continue
      ;;
  esac
  SCAN_OBJECTS+=("$object")
done
if [[ "$(uname -s)" == "Darwin" ]]; then
  LINK_GC=(-Wl,-dead_strip)
else
  LINK_GC=(-Wl,--gc-sections)
fi
"$CXX" -m64 "${LINK_GC[@]}" -std=c++17 hard-scan.o \
  "${SCAN_OBJECTS[@]}" -o hard-scan

./hard-scan "$PREDICTOR_INPUT" "$DICT" \
  >"$OUT_DIR/hard-scan.csv" 2>"$OUT_DIR/hard-scan.stderr.log"

python3 - "$OUT_DIR/hard-scan.csv" "$OUT_DIR/hard-candidates.csv" <<'PY'
import csv
import sys

src, dst = sys.argv[1:]
rows = []
with open(src, newline='') as f:
    for row in csv.DictReader(f):
        for key in ('full_fx2_4byte_bits','ppmd_4byte_nll_bits','ppmd_minus_fx2_bits'):
            row[key] = float(row[key])
        row['position'] = int(row['position'])
        rows.append(row)

# Pick difficult blocks that are separated enough not to be the same local
# event, while keeping PPMD NLL bounded so exact joint-rank search is feasible.
def separated_top(pool, limit=12, separation=64):
    chosen = []
    for row in pool:
        if all(abs(row['position'] - prior['position']) >= separation for prior in chosen):
            chosen.append(row)
            if len(chosen) == limit:
                break
    return chosen

moderate = [r for r in rows if 8.0 <= r['ppmd_4byte_nll_bits'] <= 18.0]
moderate.sort(key=lambda r: (-r['full_fx2_4byte_bits'], r['position']))
chosen = separated_top(moderate)

fields = ['position','actual','full_fx2_4byte_bits','ppmd_4byte_nll_bits','ppmd_minus_fx2_bits']
with open(dst, 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=fields)
    w.writeheader()
    w.writerows(chosen)

print('selected hard-tail candidates:')
for row in chosen:
    print(row['position'], row['full_fx2_4byte_bits'], row['ppmd_4byte_nll_bits'])
PY

cat "$OUT_DIR/hard-candidates.csv"
