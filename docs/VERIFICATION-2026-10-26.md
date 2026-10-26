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

## Ring-only presentation and native artwork export

The separate wheel now omits the full dark backing disc and instructional text.
Only the item ring, labels/ammo, selected item and page count remain. Its native
menu post effect remains suppressed. While the live wheel owns the panel, an
unready texture/pose no longer falls back to the original full-screen UI, which
contains the veil and controls bar. Original-wheel settings cannot bypass lifting
while the live wheel is active. `vr wheelshot <tag>` exports the actual overlay
texture; tags are restricted to filename-safe characters.

Simulator session 20261026-135545: exact 1024x1024 overlay and two-eye compositor
captures confirm clear surroundings/centre and an undarkened visible world.
The selected overlay has 78.37% fully transparent pixels, including its entire
outer border. Native wheel post-effect/slowdown skip counters both advanced once.
Measured 205.3 engine Presents/s, 102.65 stereo pairs/s, about 9.74 ms/pair, at
3286x3555 per eye with unchanged graphics. No stage failures, unpaired/dropped
frames or TAA-eye disagreements. B cancelled without equipping. Release build
passed; interaction and host math were unchanged in this presentation revision.

75 original wheel textures were decoded read-only from the installed game:
47 from lib_wheel and 28 from quick_access_wheel. Type-8 Iggy metadata supplies
dimensions, formats, offsets and IDs; BC7 and RGBA data decode to individual
PNGs with original alpha. Local outputs, manifests, contact sheets and a ZIP are
in local/wheel-renders. These native images are previews, not yet integrated into
the custom label-based live wheel. Game archives and extracted artwork are not
included in source control. Format references: chipolux/disrev and the JPEXS
IggyTextureMetadata/IggyTextureDecoder source.


## PSVR2 hand wheel and frame pacing follow-up

User confirmed trigger effects feel good; no trigger policy, effect, helper,
or trigger configuration is changed. Latest hand controls supersede the stick
controls in the historical verification above; see NATIVE-WHEEL-PREVIEW.md.

The user's 0.5.0 announcement screenshot describes the synchronized stereo
already enabled here (`Stereo3D=1`), not a missing update. It costs two engine
renders per complete stereo pair; switching to mono would remove depth.

Evidence retained from the real 15:05-15:15 SteamVR session: heartbeat reported
22.22 ms predicted display period (a 45 Hz application interval); compositor
reported target 90 and cumulative CPU 9.178 ms/GPU 0.718 ms. Those cumulative
figures include menus and are not a gameplay benchmark. The heartbeat's 43.1 ms
`pair gap` is pose prediction age, NOT measured stereo frame interval. The
237.1 engine FPS at shutdown is after XR exit, NOT VR performance.

The mod calls xrBeginFrame at Present, after the engine has rendered. Thus the
runtime's GPU accounting does not reliably represent the full engine workload.
Separately, RenderRes's former `gpu` value is the elapsed GPU timestamp interval
between Presents, including idle; renamed `gpu_present_interval`. Added
`stereo_fps` based on successful complete-pair submissions over a second, with
stale samples cleared. This is application stereo throughput, not headset
refresh or generated/reprojected frames.

Confirmed code inefficiency: the frame pump requested its next xrWaitFrame only
when the next finished image reached BeginFrame. After successful xrEndFrame it
now requests the next wait immediately, overlapping the wait with engine work.
Only one wait may be outstanding; the bounded wait, session-ending and quiet-game
keep-alive paths are retained. Failed submissions do not prefetch. The periodic
hand-palette diagnostic is limited to once per frame rather than once per draw.
No reduction to render resolution, mono fallback, SteamVR profile changes or
trigger changes is bundled with this experiment. Game VSync was already off.

API basis: [Khronos frame synchronization](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/rendering.adoc):
xrWaitFrame may run on another thread, must be externally serialized, and the
previous wait must have a matching begin. Requesting after successful end
satisfies that ordering. The pump owns the sole wait call and hands off its
frame state under its existing mutex.

Validation: six host checks pass and the Release DLL builds. No live performance
improvement is claimed yet; the user needs to launch and compare normal gameplay
with the new stereo_fps reading. CPU engine cost, GPU engine cost and runtime
throttling remain possible additional limits. Neither 90 Hz performance nor a
complete cure for the reported low FPS has been established.

## Graphics hooks, distant blur and lens effects (evening)

Reproduced the real-headset hook failure without a headset by launching with
MockXR noHeadset=1, waiting for the menu videos, then enabling the simulated
headset. Baseline 9fd9bcc misses later Map/Unmap/ExecuteCommandList implementations:
361 missing TAA view shadows and automatic TAA fallback. The repaired build handles
three successive implementations, keeps per-eye TAA active, and reports zero
missing shadows in the loaded Void scene. Nested wrapper/native calls are counted
only once using a per-thread, per-API guard. Failed detour transactions are not
registered as successful hooks; additional implementation capacity is bounded.

Both baseline and repaired build reached 90 stereo pairs/sec in this simulator
case at 3076x3140 eye textures and 2873x3741 engine rendering. This proves the hook
repair, NOT a headset FPS improvement. SteamVR throttling/frame scheduling and
AER stick-turn blur remain unresolved. No early xrBeginFrame scheduling change,
single-pass stereo or AFW implementation is included.

Added Display.DisableDepthOfField=1 and DisableLensEffects=1 defaults; the latter
disables r_skipLensFlares, r_skipEmissiveGlare and r_skipBloom passes by setting
their skip flags to 1. Display.TextureAnisotropy defaults to 16. These use the
existing direct numeric CVar writer: queued console commands returned success
without applying these settings in the startup test. Final build startup readback
confirms all four skip flags=1 and anisotropy=16. Numeric values survive loading
into the save; CVar string values are unchanged by this runtime-only writer.
Setting either disable option to 0 leaves the game's value alone on next launch.
Scene lighting and reflective materials are not globally disabled. Headset
perception of the reported flashing cannot be certified by the simulator.

Depth-of-field was enabled; it is a plausible contributor to distant blur.
Earlier exec-based screenshot comparisons are not sufficient proof by themselves
because command acceptance did not guarantee application. Final direct-setting
capture is local/fps-blur-repair/DVR_effects_off_eye0.png. Baseline and repaired
logs/heartbeats are retained in that local directory, plus final startup readback.
Release build and all eight host checks pass. Trigger/haptic policy, body settings
and Toolkit settings are unchanged. Simulator overrides are removed after testing;
saves and game config were verified byte-for-byte unchanged and left untouched.
Simulator-altered hand calibration was restored after backing up the current files.
