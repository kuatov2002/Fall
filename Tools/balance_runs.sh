#!/bin/sh
# Usage: Tools/balance_runs.sh <tag> <good|avg> <runs> [extra args]
# Full-game bot runs without rendering, fixed 30 fps step, one seed per run. Prints passed/deaths per run.
export MSYS_NO_PATHCONV=1
UE="/c/Program Files/Epic Games/UE_5.9/Engine/Binaries/Win64/UnrealEditor.exe"
TAG="$1"; SKILL="$2"; N="$3"; shift 3
for i in $(seq 1 "$N"); do
  DIR="Balance/$TAG/${SKILL}_$i"
  rm -rf "Saved/Review/$DIR"
  "$UE" "C:/Users/chann/Fall/Fall.uproject" /Game/Freefall/Maps/Freefall -game -nullrhi -benchmark -fps=30 -nosound -unattended -nosplash \
    -FallReview="$DIR" -FallBot=full -FallBotSkill="$SKILL" -FallSeed="$i" -FallNoShots -FallDuration=480 "$@" -log -abslog="C:/Users/chann/Fall/Saved/Logs/balance_${SKILL}_$i.log" >/dev/null 2>&1
  python -c "import json,sys;m=json.load(open(sys.argv[1]));print(sys.argv[2], 'passed' if m['passed'] else 'FAILED', 'deaths', m['deaths'], 'time', round(m['duration_s']))" "Saved/Review/$DIR/metrics.json" "${SKILL}_$i" 2>/dev/null || echo "${SKILL}_$i no metrics"
done
