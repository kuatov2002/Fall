#!/bin/sh
# Re-records the procedural reload and rebuilds A_Reload_Freefall / AM_Reload_Freefall from it.
export MSYS_NO_PATHCONV=1
cd "$(dirname "$0")/.."
timeout 600 Tools/run_review.sh /Game/Freefall/Maps/Freefall IT3/bake -FallBakeReload -FallNoIntro > /dev/null 2>&1
bash Tools/run_python.sh Tools/make_reload_anim.py | grep -E "LogPython: (Warning: RELOAD_ANIM (sequence|montage)|Error)" | head -5
