# Continuation implementation and verification

This supersedes the pending implementation descriptions in CONTINUATION_HANDOFF.md.
Source: fa23bc0 (left-hand dot), 2d3a427 (live wheel), a8499e9 (scripted ownership/handback).

The requested aiming aid is a small outlined white dot driven by the LEFT controller's
aim ray. Left-controller yaw moved it; right-only yaw did not. Invalid left tracking
and menus hide it. The native crosshair is temporarily suppressed and restored when
the feature relinquishes ownership. Projection uses each presented eye.

The live wheel uses native inventory availability, localized names and native equip,
with eight items per page. Hold left grip, point either stick, release to confirm;
centre or B cancels. X/Y page, A mana, left-stick click health. Normal menus still
pause. The custom wheel suppresses only its own native slowdown and menu effect.
Missing tracking, stale player, recenter, resource failure and focus loss cancel
confirmation. Original wheel remains available with Gameplay.LiveWheel=0.

Scripted ownership now also guards aim-anchor, bound-object and weapon fallback
paths. Arms/equipment ease back for 0.25 seconds after native ownership ends;
the native states, rather than a timer, decide when that happens. Both eye views
share the blend value. Learned grips and weapon size are preserved. Equipped-hand
scale is independently adjustable; candidate hand scales are both 0.85.

## Runtime evidence and limits

Windows, Dishonored 2 1.77.9.0, isolated OpenXR simulator. Requested 3072x3264 per
eye, rendered 3286x3555 with the existing symmetric-frustum padding. Graphics and
render scale were not reduced. These are simulator results, not headset results.

- Shared HUD, left-hand dot, native crosshair restoration and smaller hands checked.
- Normal gameplay approximately 194-211 engine Presents/s, 97-105 complete stereo
  pairs/s, approximately 9.5-10.3 ms/pair in the sampled view.
- Paged live wheel approximately 182 Presents/s, 91 pairs/s, 11 ms/pair. Measured
  game-clock/wall-clock ratio 0.997 while open; world remains live in stereo.
- Native equip verified with pistol in session 124643 and grenade in 131511.
  Native inventory ordering varies: do not infer an item from its list index.
- Centre cancellation, B held for a second, no reopen while grip held, left/head
  tracking loss, pause via the VR menu button, resume and reopen passed.
- Artificial scripted ownership with moving hands returned native arms/equipment;
  release trace was monotonic and reached full tracking. A 0.25 s transition had
  fewer samples than steady play; the trace/capture run is not a GPU benchmark.
- Actual OS focus-loss cancellation, every cutscene, skip/interruption and death
  are not claimed tested. The attempted direct console save-load shortcut did not
  enter gameplay and is not evidence of load correctness.
- Headset perspective, finger contact across all equipment, dot size and comfort
  remain user checks. No headset gameplay performance result is claimed.

Host CMake checks for arm calibration/handback and wheel selection pass. Release
DLL builds successfully. Game-derived decompilation and captures stay under local/.
Final a8499e9-source runtime check: the normal Continue load entered gameplay;
forced Running:3 ownership suspended arms and release produced 46 intermediate
trace samples, monotonic to 1, with successive stereo samples sharing each weight.
Steady play resumed at 190.6 Presents/s (95.3 pairs/s, 10.5 ms/pair), 3286x3555,
with zero staging failures, unpaired frames, dropped frames or TAA-eye disagreements.
This later run supersedes the low sample density of the earlier capture trace.
The candidate and matching official rollback files are prepared under
local/headset-candidate-20261026; manifest.json records exact binary hashes.
No single-pass stereo implementation is claimed: current rendering remains two
eye views, with one simulation step per stereo pair and the shared HUD plane.
