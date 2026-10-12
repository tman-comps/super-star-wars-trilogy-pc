#!/bin/sh
# Empire coverage: every gameplay scene and cutscene from the scene table, two at a time.
cd "$(dirname "$0")/../.."
OUT=${OUT:-build/cov1}; mkdir -p $OUT
{
for s in 01 02 06 03 05 04 09 15 14 07 08 0C 0D 0E 0F 13 12 10 17 18 16; do echo "04 $s 15000"; done
for t in "0E 0A" "0E 1A" "12 00" "12 02"; do echo "$t 15000"; done
for s in 1B 1E 1F 20 21 22 25 24 2C 31 33 32 29 28 37 30 34 2F 2A 27 2B 36 35 1D; do echo "0A $s 2500"; done
echo "06 20 3000"
} | xargs -P 2 -L 1 sh -c 'python3 tools/emu/cover_esb.py rom/sesb.sfc $0 $1 $2 ${SEED:-1} '$OUT'/c$0$1 '$OUT'/s$0$1 > /dev/null 2>&1; echo "$0 $1 done"'
