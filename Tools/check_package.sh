#!/bin/sh
# Checks the packaged build itself, not the editor: runs Build/Package with the weapon test and fails on any
# package missing from the cook (SkipPackage) or any weapon/reload/recoil failure.
#   Tools/check_package.sh
cd "$(dirname "$0")/.."
PKG=Build/Package/Windows
OUT=$PKG/Fall/Saved/Review/pkgcheck
rm -rf "$OUT"
rm -f $PKG/Fall/Saved/Logs/Fall.log
timeout 900 "$PKG/Fall.exe" /Game/Freefall/Maps/Freefall -windowed -ResX=1280 -ResY=720 -benchmark -fps=30 -nosound -unattended \
  -FallReview=pkgcheck -FallWeaponTest -FallIntro -FallShot=1000 > /dev/null 2>&1
MISSING=$(grep -h "SkipPackage" $PKG/Fall/Saved/Logs/Fall.log | grep -v "/ToolsetRegistry/" | sed 's/.*SkipPackage: //; s/ (0x.*//' | sort -u)
echo "missing packages: ${MISSING:-none}"
python -X utf8 -c "
import json, sys
m = json.load(open('$OUT/metrics.json'))
print('weapon_test_pass', m.get('weapon_test_pass'), 'states missing', m.get('weapon_states_missing'), 'bad frames', m.get('weapon_in_hand_bad_frames'))
print('reloads', [r['pass'] for r in m.get('reloads', [])], 'shots without recoil', m.get('shots_without_recoil'))
sys.exit(0 if m.get('weapon_test_pass') else 1)
" && [ -z "$MISSING" ] && echo PACKAGE_CHECK_PASS || { echo PACKAGE_CHECK_FAIL; exit 1; }
