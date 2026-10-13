#!/bin/sh
# Return of the Jedi: compare recompiled vs interpreted runs per stage (one hash of WRAM, VRAM, CGRAM
# and OAM per frame must match on every frame). usage: verify_rotj.sh rom [frames] [seed]
# Each stage is entered through the scene the game plays just before it (stage:entry, the warp target;
# the input script skips the cutscene with Start): warping straight into a stage skips that scene's
# setup, and the speeder bikes (1211) then crash in the original code too. SCENES="0402:0402" limits
# the list. 0617:062C is the ending, from the Falcon's escape to the credits and the title. "full" is
# a new game with the developer stage skip (CLEAR, controller 1 B+Start); the Death Star reactor run
# 0A01 has no skip, so that run stops there.
# Needs: make rotj_test
ROM=$1; N=${2:-6000}; SEED=${3:-7}; OUT=${OUT:-build/verify-rotj}; mkdir -p $OUT
fail=0
for st in ${SCENES:-1412:1412 0402:0402 0407:0407 0401:061C 0408:061B 0405:061D 0406:0406 1211:0620 040A:0621 040B:040B 0403:0622 0C10:0625 0409:0623 040F:0624 1413:0626 040E:0628 040C:040C 040D:0629 0A01:062D 0A02:0627 0617:062C full}; do
  t=${st%%:*}; e=${st#*:}
  if [ $t = full ]; then
    CLEAR=900 ./rotj_test $ROM ${FULL:-60000} interp $SEED > $OUT/$t.i 2>/dev/null &
    CLEAR=900 ./rotj_test $ROM ${FULL:-60000} recomp $SEED > $OUT/$t.r 2>$OUT/$t.miss
  else
    m=$(echo $e | cut -c1-2); s=$(echo $e | cut -c3-4)
    ./rotj_test $ROM $N interp $SEED 0 $m $s > $OUT/$t.i 2>/dev/null &
    TRACE=1 ./rotj_test $ROM $N recomp $SEED 0 $m $s > $OUT/$t.r 2>$OUT/$t.miss
  fi
  wait
  if [ $t != full ] && ! grep -q "mode $(echo $t | cut -c1-2) scene $(echo $t | cut -c3-4)" $OUT/$t.miss; then echo "$t NOT REACHED"; fail=1
  elif cmp -s $OUT/$t.i $OUT/$t.r; then echo "$t OK  $(grep misses $OUT/$t.miss)"; else echo "$t MISMATCH at frame $(diff $OUT/$t.i $OUT/$t.r | sed -n 2p)"; fail=1; fi
done
exit $fail
