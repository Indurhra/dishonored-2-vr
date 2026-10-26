# Indurhra fork status

Based on thecosmictangerine-cloud/dishonored-2-vr. Original authorship and licenses
are retained. The commit history records individual changes rather than replacing
upstream history.

## Included work
- Live hand-selected weapon/power wheel using native artwork with a translucent
  ring-only backing, selection highlighting, all items on one ring, no selection dwell.
- Left-hand aiming dot, shared stereo HUD plane, palm calibration and arm ownership fixes.
- Controller VR menu: hold X for three seconds, sticks to navigate, A to confirm, B back.
- Story Heart recovery for the legacy all-items cheat interaction.
- Graphics-context/per-eye tracking fixes, frame-cap guard, lens-effect suppression,
  clarity controls and diagnostic timing.
- Left-only PSVR2 adaptive triggers: pistol strength 5, crossbow strength 7, weapon
  break zones 2/3, firing at 80% pull with release at 70%. Power effect unchanged.
  Physical break/firing alignment still needs user validation.

## Experimental and unresolved
`vr directturn 1` enables the direct visual-yaw experiment. It is OFF by default,
not installed in the last trigger-only build, and has no completed headset validation.
`vr directturn 0` disables it. It is not a proven judder fix.

The PSVR2 45 FPS runtime lock was reproduced and addressed on the test machine,
but machine-specific SteamVR configuration is not automatically applied by this fork.
Distant blur, moving-scene frame drops and stick-turn judder are not declared fixed.
A stationary 45-second CPU-scheduling/SteamVR capture had no dropped/reprojected
frames and cannot establish the cause of gameplay drops. No CPU/cooling tweaks are
part of this source upload.

## Validation and local exclusions
Eight host tests passed for the latest trigger changes, including fake-Toolkit IPC
watchdog checks, exact effect values and firing-threshold hysteresis. These do not
replace a physical headset test. See PSVR2-TRIGGERS.md and VERIFICATION-2026-10-26.md.
Build folders, local tools/dependencies, backups, raw system traces, saved games and
installed binaries are excluded. No binary release is published by this upload.
