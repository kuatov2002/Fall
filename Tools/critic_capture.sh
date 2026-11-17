#!/bin/sh
# One critic round's capture: a full bot run with the cutscene, a shot every 0.5 s, the round number as the seed,
# then the 30-frame selection; the raw PNGs are deleted afterwards (the disk is nearly full).
# Usage: Tools/critic_capture.sh <round>
cd "$(dirname "$0")/.."
export MSYS_NO_PATHCONV=1
R=$1
RUN=IT3/crit_run$R
# Long runs sometimes die (a GPU hang in Nanite shadows, or a crash without a usable stack): up to 3 attempts.
for TRY in 1 2 3; do
  timeout 1500 Tools/run_review.sh /Game/Freefall/Maps/Freefall $RUN -FallBot=full -FallIntro -FallShot=0.5 -FallDuration=260 -FallSeed=$R > /dev/null 2>&1
  [ -f Saved/Review/$RUN/shots_index.json ] && break
  echo "capture attempt $TRY failed: $(grep -m1 -E 'GPU crash|Unhandled Exception' Saved/Logs/review.log)"
done
python -X utf8 Tools/critic_round.py select $R Saved/Review/$RUN && find Saved/Review/$RUN -maxdepth 1 -name "shot_*.png" -delete
