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

## Half-rate lock investigation (19:10 headset session / 19:30 simulator)

The headset session after the graphics-hook repair has zero missing view shadows,
no eye-phase disagreement and no TAA fallback, yet native stereo still submits at
half rate. SteamVR's steam.app.403640 profile has motionSmoothingOverride=2 and no
framesToThrottle setting. Toolkit's DishonoredVR profile has no explicit frame
limiter value. The recorded longest xrWaitFrame is below 0.005 s, so the old logs
do not support assuming xrWaitFrame is where the persistent wait happens.

Inspected existing IDA decompilation and fresh executable disassembly: the graphics
frame path at RVA 0x359240 loads com_timeScaler's numeric float and calls the
spin/yield limiter at 0x462E0. The settings UI maps the scale to 30 * scale FPS.
Our prior override wrote 240 FPS once, before save loading, then returned forever
while the requested setting stayed unchanged. It could not recover a later reset.
The user's archived scale is 3 (90 FPS); its actual runtime value during the
headset lock was NOT recorded. This is a candidate cause, not a proven diagnosis
of that headset session.

Changes: enforce the explicit StereoFrameLimit during a running synchronized VR
session; detect a replacement value and preserve it for restoration when leaving
that mode. Disable desktop Present sync intervals only while XR is running,
following the separation of desktop/headset pacing used in the D1 reference.
Desktop flags and flat-game presentation are unchanged. Actual requested desktop
sync was already 0 in the simulator; no gain is attributed to that safeguard.

Added bounded-window wall-clock measurements for xrWaitFrame, xrBeginFrame,
xrEndFrame, swapchain acquisition/wait, the real desktop Present, and time outside
Present. These are not GPU timings and worker-thread waits can overlap rendering.
Heartbeat snapshots reset each second and every fifth snapshot is retained in the
log. Current engine cap and ownership are reported alongside the samples. These
will distinguish engine/driver waits from runtime begin/end waits; xrBeginFrame
still occurs after engine drawing, a separate scheduling concern not changed here.
See https://github.com/KhronosGroup/OpenXR-Guide/blob/main/chapters/frame_submission.md.

Simulator regression at 3076x3140 eyes, engine 2873x3741, latest save:
- Deliberately configured 90 engine FPS: exactly 45 stereo FPS, xrWait ~0.001 ms,
  outside Present ~10.44 ms, desktop Present ~0.097 ms.
- Restored 240 target and injected com_timeScaler=3: detected/reapplied 240 next
  Present; approximately 155 engine / 77.7 stereo FPS, outside Present ~3.47 ms.
- Switching to AER restores the replacement game limit (scale 3 / 90 FPS).
- All eight host checks pass; Release build succeeds.

Evidence and current-install backup: local/half-rate/. This reproduces and fixes
one mechanism giving precisely the reported lock. Actual SteamVR lock removal
requires a headset run; simulator results do not establish 90 FPS in this newer
scene. No trigger, body, lens-effect, resolution or SteamVR-setting changes.

## Real PSVR2 half-rate diagnosis (evening, 26 October)

Real SteamVR/OpenXR PSVR2, 90 Hz, native synchronous stereo, DLL 0baafff.
Eyes 3076x3140, engine 2872x3743. The engine cap stayed at 240; the earlier
cap repair was not sufficient to resolve this actual headset lock.

Controlled same-process, same-scene A/B/A/B test:

| SteamVR asynchronous mode | Stereo FPS | Desktop Present mean | Compositor observation |
| --- | --- | --- | --- |
| Enabled (baseline) | 45–46 | ~6.4 ms | two presents per frame, async flag 4 |
| Disabled | ~90 | ~0.11 ms | one present per frame |
| Re-enabled | ~45.4 | ~6.37 ms | half-rate returns |
| Disabled again | ~89.5–90 | ~0.12 ms | one present, flags 0, no drops in sampled frames |

The causal workaround is per-application `disableAsync=true`, exposed by this
SteamVR build as **Use Legacy Reprojection Mode**. Motion smoothing was already
off; disabling it alone does not disable the asynchronous compositor path.
Explicit `framesToThrottle=0` and `additionalFramesToPredict=0` alone did not
resolve the lock. Keep `motionSmoothingOverride=2` (off).

This isolates an interaction with the SteamVR asynchronous presentation path.
It does not yet identify the precise internal SteamVR/driver defect. The game
has enough throughput for 90 native stereo in this tested scene. The small
fpsVR GPU numbers are not complete engine-render timings: frame submission
starts after much of the engine work. Do not interpret them as total frame cost.

NVIDIA VSync was globally forced on. Testing application-controlled VSync did
not remove the lock. That global experiment was restored to its original value;
the SteamVR workaround continued to produce ~90 FPS. No mod DLL changes were
needed for this result. Fresh-process behaviour with the restored NVIDIA value
has not yet been separately tested.

User reports the issue across their PSVR2 VR games, without remembering titles.
Applied the workaround to 22 existing identifiable game profiles, backing up
all original values. Removed explicit half-rate limits found in Ready or Not,
Skyrim VR and Cemu. Other games are configured, not performance-validated.
These profiles apply when those games use SteamVR, regardless of headset;
Quest VDXR does not use them. Unknown/unidentified profiles and utility apps
were not modified. New game profiles do not inherit this per-app workaround.

Evidence, complete affected-game list and backups are in
`local/psvr2-system-investigation/`: `controlled-abab.log`, three heartbeat
snapshots, `steamvr-before-live-tests.json`, `game-profiles-before.json`,
`game-profiles-applied.json`, and `global-vsync-before.json` (already restored).
The game remains running for the user's visual test. Trigger effects unchanged.
Disabling async can change judder behaviour if a heavier scene misses refresh;
this is a verified half-rate workaround, not a guarantee of 90 FPS everywhere.

## Follow-up: full user test and fresh-session checks

The complete 19:56–20:22 session contains the earlier async A/B tests; its
aggregate reprojection percentage cannot describe only the user's later play.
fpsVR reports median 89 FPS, average 84.22, 2,389 compositor drops, CPU p99 11.9 ms
and p99.9 15.5 ms. GPU p99 is 1.7 ms, but this mod begins the OpenXR frame after
engine rendering, so this number does not include all engine GPU work.

Decoded fpsVR's binary history using its viewer's actual BinaryReader field
layout (header uint16/int32; 30-byte per-second records). CSV retained locally.
Approximate 20:12–20:22:30 interval: median per-second minimum/maximum FPS both
90; 596/630 seconds have a maximum >=89, 20 have a minimum below 80. Several
50–75 ms spikes align with journal/pause/load transitions. CPU temperature median
72 C, p95 82 C. The two 99 C records still report 90 FPS and CPU maxima under
8 ms, so these measurements do not establish thermal throttling as the cause.
The history lacks separate per-second dropped/reprojection counters; do not
claim the entire user test had zero reprojection.

Fresh process 38548, original full resolution and restored original NVIDIA
setting: 30-second initial capture 2,697 presents, zero drops/reprojections;
second 20-second capture 1,797 presents, zero drops/reprojections. IMPORTANT:
these were dashboard-visible/controller-untracked samples, not representative
hand-active gameplay despite the second filename `controllers-baseline.json`.
After dashboard closure: 30 seconds, 2,693 presents, 3 drops, zero reprojections;
subsequent 60 seconds, 5,385 presents, 9 drops, zero reprojections (6 drops inside
one timeout). Controllers still reported disconnected and `maxturn=0`; the
filename `turn-test.json` is NOT evidence of a successful stick-turn test.

NVIDIA utilisation was 88–97% at full scale, reduced to 67% in a temporary
0.75-scale comparison. Both steady configurations reached 90. Restored scale
1.0 and verified 2872x3743 engine size. No persistent graphics reduction.
CPU boost ~158–160% of nominal, system-wide DPC ~0.24–0.29%; these short samples
do not establish a scheduling/interrupt problem or exclude brief spikes.
No BIOS, affinity, priority or power-plan modifications made.

Prepared an opt-in `vr directturn 1` experiment: normal-play visual yaw uses the
VR room anchor plus predicted head yaw rather than mouse-servo corrections.
Keeps existing game pitch/roll, scripted-animation and possession handling,
and the existing shared left/right pose. Default remains off. Built Release,
eight existing host tests pass (these do NOT validate perceived camera motion).
Not installed or visually verified yet; requires a controller-active movement
test. The current installed DLL remains 0baafff. Do not describe this as a
confirmed fix. All evidence/scripts under local/psvr2-system-investigation/.

## External CPU / Process Lasso audit and reversible comparisons

Read C:/ProgramData/ProcessLasso/config/prolasso.ini and enforcement log.
ProBalance OocOn=false, efficiency mode rules empty, CPU limiter/throttle rules
empty, SmartTrim disabled, persistent priorities empty. No affinity rule for
Dishonored2.exe, vrserver.exe or vrcompositor.exe. vrmonitor.exe and several
other games have 0-15 affinity rules. Current game uses all 32 logical CPUs;
compositor/server priority High, game Normal. No relevant Lasso enforcement
matching the game stalls. Existing Lasso rules were NOT changed.

Windows CPU-set API confirms performance logical CPUs 0-15 (efficiency class 1),
efficiency cores 16-31 (class 0). Tested only this game's current affinity,
not a permanent Lasso rule. Same stationary game scene; headset active but
controllers untracked, so these comparisons do not validate active-turn judder.
Each capture lasted 30 seconds; frame counts below are compositor counters.

| Test | Presents | Dropped | Reprojected | Client interval median / p99 ms |
| --- | ---: | ---: | ---: | --- |
| Original all cores A | 2697 | 0 | 0 | 6.978 / 8.241 |
| Performance cores only B | 2695 | 2 | 0 | 6.696 / 7.791 |
| Restored all cores A2 | 2696 | 0 | 0 | 6.974 / 8.271 |
| Existing Bitsum Highest Performance plan | 2698 | 0 | 0 | 6.872 / 7.588 |
| CAM interface BelowNormal priority | 2696 | 0 | 0 | 7.017 / 8.942 |

All were approximately 90 FPS. Some p99 differences are small and do not prove
an improvement to the reported symptom. Kept NONE of these changes. Restored
affinity 0xffffffff, original Ultimate Performance GUID
26436b6e-2c49-4b72-a133-506bec39f35d, and CAM PID22440 Normal priority. Never
stopped cooling services or changed their configuration. Final restoration
capture stored as external-restored-final.json.

Other checks: ~13.7 GB available RAM, almost no hard paging, sampled disk latency
~0.1-0.5 ms. No WHEA, display-driver reset or Kernel-Processor-Power event 37
found in available System log over seven days. Brief DPC/interrupt samples do
not show a sustained interrupt storm, but cannot exclude transient spikes.
NZXT CAM/helper together used about 1.2 CPU-seconds in a 3-second background
sample; Lasso GUI ~0.03 CPU-seconds. CAM priority test did not improve timing.
Intel XTU service exists; previous-day log reports compatibility restrictions.
No XTU, BIOS or voltage changes. Board MSI Z790 GAMING PLUS WIFI, BIOS H.80 dated
2025-04-11. Boot timer configuration query was denied by Windows permissions;
no boot changes attempted. Cannot claim all possible external causes ruled out.

Unresolved: a representative moving-scene CPU scheduling/GPU trace and visual
stick-turn test. The old 45 FPS lock remains reproducibly addressed by SteamVR
async configuration; no new system-wide CPU fix was demonstrated by this audit.
