#!/bin/sh
# Return of the Jedi coverage on the port's own core (interpreter): a full run from a new game to
# the ending with the developer stage skip, plus every stage played in god mode from a warp.
# Writes build/rotj/cdl/*.cdl; then:
#   python3 -I tools/cdl_to_hints.py roms/srotj.sfc build/rotj/cdl_hints.txt build/rotj/cdl/*.cdl
#   cat analysis/rotj/hints.txt build/rotj/cdl_hints.txt > build/rotj/h.txt
#   python3 -I tools/trace.py roms/srotj.sfc analysis/rotj/map.json build/rotj/h.txt
# usage: tools/emu/cover_rotj.sh [frames_per_stage] [seed]   (needs: make rotj_test)
cd "$(dirname "$0")/../.."
N=${1:-12000}; SEED=${2:-1}; OUT=build/rotj/cdl; mkdir -p $OUT
{
echo "full 90000"
# each stage entered through the scene before it (see verify_rotj.sh), plus the ending
for t in 1412 0402 0407 061C 061B 061D 0406 0620 0621 040B 0622 0625 0623 0624 0626 0628 040C 0629 062D 0627 062C; do echo "$t $N"; done
} | xargs -P ${JOBS:-4} -L 1 sh -c '
  if [ $0 = full ]; then CDL='$OUT'/full_'$SEED'.cdl GOD=1 CLEAR=900 TRACE=1 ./rotj_test roms/srotj.sfc $1 interp '$SEED' > /dev/null 2> '$OUT'/full_'$SEED'.log
  else CDL='$OUT'/s$0_'$SEED'.cdl GOD=1 TRACE=1 ./rotj_test roms/srotj.sfc $1 interp '$SEED' 0 $(echo $0 | cut -c1-2) $(echo $0 | cut -c3-4) > /dev/null 2> '$OUT'/s$0_'$SEED'.log; fi
  echo "$0 done"'
