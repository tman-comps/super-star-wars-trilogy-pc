#!/bin/sh
# Compare recompiled vs interpreted runs on every scene. usage: tools_verify.sh rom frames
ROM=$1; N=${2:-4000}; OUT=${OUT:-build/verify}; mkdir -p $OUT
fail=0
for t in ${SCENES:-0410 0E04 040B 0401 0409 040A 0E05 040D 040C 040E 0400 0411 0412 0808 0E14 1006 0802 0A1F 0A24 0A16 0A25}; do
  m=$(echo $t | cut -c1-2); s=$(echo $t | cut -c3-4)
  ./ssw_test $ROM $N interp 7 0 $m $s > $OUT/$t.i 2>/dev/null &
  ./ssw_test $ROM $N recomp 7 0 $m $s > $OUT/$t.r 2>$OUT/$t.miss
  wait
  if cmp -s $OUT/$t.i $OUT/$t.r; then echo "$t OK  $(head -1 $OUT/$t.miss)"; else echo "$t MISMATCH at frame $(diff $OUT/$t.i $OUT/$t.r | sed -n 2p)"; fail=1; fi
done
exit $fail
