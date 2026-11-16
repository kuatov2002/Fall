#!/bin/sh
# Usage: Tools/run_review.sh <map> <review-subdir> <extra args...>
# Runs the game with a fixed 30 fps step and the review bot. Output goes to Saved/Review/<review-subdir>.
export MSYS_NO_PATHCONV=1
UE="/c/Program Files/Epic Games/UE_5.9/Engine/Binaries/Win64/UnrealEditor.exe"
MAP="$1"; shift
DIR="$1"; shift
rm -rf "Saved/Review/$DIR"
"$UE" "C:/Users/chann/Fall/Fall.uproject" "$MAP" -game -windowed -ResX=${RESX:-1280} -ResY=${RESY:-720} -benchmark -fps=30 -nosound -unattended -nosplash -FallReview="$DIR" "$@" -log -abslog="C:/Users/chann/Fall/Saved/Logs/review.log"
