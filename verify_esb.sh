#!/bin/sh
# Empire: compare recompiled vs interpreted runs per scene. usage: verify_esb.sh rom frames
ROM=$1; N=${2:-3000}; OUT=${OUT:-build/verify-esb}; mkdir -p $OUT
fail=0
for t in ${SCENES:-0401 0402 0406 0403 0405 0404 0409 0E0A 0415 0414 0407 0408 1200 040C 040D 040E 040F 0413 0412 0410 0E1A 0417 0418 0416 1202}; do
  m=$(echo $t | cut -c1-2); s=$(echo $t | cut -c3-4)
  ./esb_test $ROM $N interp 7 0 $m $s > $OUT/$t.i 2>/dev/null &
  ./esb_test $ROM $N recomp 7 0 $m $s > $OUT/$t.r 2>$OUT/$t.miss
  wait
  if cmp -s $OUT/$t.i $OUT/$t.r; then echo "$t OK  $(head -1 $OUT/$t.miss)"; else echo "$t MISMATCH at frame $(diff $OUT/$t.i $OUT/$t.r | sed -n 2p)"; fail=1; fi
done
exit $fail
