#!/bin/sh
# Full review batch: whole fall, chase from 118 s, finale with parachute, animation gallery. Usage: Tools/stage_runs.sh <tag>
T="$1"
timeout 2400 Tools/run_review.sh /Game/Freefall/Maps/Freefall M4/full_$T -FallBot=full -FallShot=0.5 -FallDuration=185 -FallBursts > /dev/null 2>&1
timeout 1800 Tools/run_review.sh /Game/Freefall/Maps/Freefall M4/chase_$T -FallBot=full -FallShot=0.5 -FallDuration=75 -FallBursts -FallStartTime=118 > /dev/null 2>&1
timeout 1200 Tools/run_review.sh /Game/Freefall/Maps/Freefall M4/finale_$T -FallShot=0.5 -FallDuration=24 -FallBursts -FallStartTime=162 -FallGiveParachute > /dev/null 2>&1
RESX=640 RESY=360 timeout 2400 Tools/run_review.sh /Game/Freefall/Maps/AnimGallery M5/gallery_$T -FallGallery=M5/gallery_$T > /dev/null 2>&1
echo ALLDONE
