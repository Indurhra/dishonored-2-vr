# DLSS / AFW implementation checkpoint

## Current state (2026-10-28)
Implementation is underway; this is a diagnostic candidate, not a completed AFW renderer.
Working branch: codex/dlss-afw, baseline 5ba6a00, checkout Q:/Modding Community/dishonored-2-dlss-afw.
Only external game implementation reference: Dishonored 1 staging cdd8d32c5.
The supplied DLSS_IMPLEMENTATION.md and AFW_RENDERING_GUIDE.md are reference evidence;
engine conventions, offsets, thresholds and presets are not copied as defaults.

Implemented and compiled:
- Native D3D11 NGX backend with separate eye features, five quality modes, lifecycle cleanup,
  explicit jitter/motion/exposure inputs, and same-image provenance checks.
- Failed evaluations invalidate exposed outputs; old eye images cannot masquerade as fresh DLSS results.
- Fifteen temporal identity/history tests.
- Hardware GPU smoke test: all five modes, 30 evaluations, finite/nonblack output readback,
  expected dimensions, mismatched guides refused, repeated shutdown and reinitialization,
  deferred context refused. Synthetic inputs only; these are not in-game quality measurements.
- Shader-reflection input audits: vr temporalinputs N and vr depthinputs N.
- Matched GPU input capture: vr temporalcapture TAG. Copies are recorded on the source context;
  event queries must complete before immediate-context readback. Captures require identical
  serial, eye and dimensions. Raw colour, motion and depth plus matrices/jitter are exported.

Not implemented/validated yet:
- Production-ready engine adapter: diagnostic DLAA and postprocessing-compatible SR now run live (details below), but redundant native passes remain.
- Production AFW foreground reconstruction and one-render stereo submission (isolated kernel exists).
- VR-menu renderer/quality settings and persistent active/fallback state.
- Measured rendering speedup, capture replay alignment and headset image quality.
No speedup is claimed.

## Actual engine observations
Simulator loaded the existing Edge of the World save, sword and crossbow visible.
Native synchronized stereo rendered 3286x3555 per eye (mock recommendation 3072x3264,
FOV -54/40/44/-55, 90 Hz pacing). Capped results are not a speedup measurement.
TAA dispatch uses deferred context kind 1; eye/view serials match the main-view ring.
Colour ro_viewcolormap: R11G11B10_FLOAT (26); motion ro_motionvectors: R16G16_FLOAT (34).
ro_downsampling_depthbuffer is R24G8_TYPELESS (44), SRV R24_UNORM_X8_TYPELESS (46),
full eye dimensions, with identifiable matching serial/eye. Other depth resources include
alpha depth and coarse pyramids: names alone are not enough to select reconstruction depth.
The main TAA and depth downsampling may record concurrently. CPU log order is not GPU execution order.
Depth and world-motion conventions were subsequently validated below; exposure and moving foreground remain open.
The backend deliberately requires an immediate context. Do not call NGX from the observed deferred hook.

## Reproducibility and rollback
Build: build-q; set process-local TEMP and TMP to local/temp on Q before invoking VS2022 CMake.
Use existing vcpkg dependencies from the original D2 checkout. Old moved build caches have obsolete C paths.
NGX SDK: NVIDIA/DLSS v310.7.0, commit a291cc7d2cc642a51566f3dfd5376f635cd1b284.
D3D11 static CRT library nvsdk_ngx_s.lib; runtime SHA256:
BE6E434A94CA32499515EB62CA0E6C274526055D568D0426E4C652DCDFB6EE6E.
Installed original DLL/settings/log backups: local/installed-baseline, hashes in manifest.json.
Original DLL SHA256: 018FC6DD55828008D6DAB5C4213091B4FB99D81DF842D7B34656C8FF700592A8.
Diagnostic DLL installed with native behavior; no production DLSS invocation exists yet.
Simulator session backup: local/sim-dlss-inputs/state.json, including savegame and game configuration.
Existing isolated mock runtime from original D2 checkout; runtime selection is game-process local.
Input-audit log: local/sim-dlss-inputs/input-audit.log.
Restore simulator override files and configuration after testing; preserve new diagnostic evidence separately.

## Measurement constraints
Earlier CPU-bound diagnosis remains provisional: EngineGap is wall time, and fpsVR GPU coverage is unverified.
Separate completed XR stereo submissions from game Presents. Exclude capture/readback stalls from timings.
Native stereo remains baseline/fallback. AFW must remove the second scene render, not merely add a warp.
The reference screenshot guides sword/hand edge checks; its unmatched background is not a ground-truth
pixel correspondence. No arbitrary offset should be fitted to that screenshot.

## Capture/replay progress
Matched captures completed for both eyes and simulated head/controller motion. Raw depth is reversed:
sky 0, nearby geometry larger. Stencil 1 isolates arms, sword and crossbow in these captures.
This is evidence for this scene, not yet a general classification guarantee for other weapons/effects.
GPU DLAA replay of all three real captures (3286x3555) produced finite nonblack reset-frame output.
No temporal convergence or jitter-sign claim follows from a reset-frame replay.

Consecutive synchronized stereo pair (serials 17803/17804) geometry check:
- World: 328867 samples; median relative depth error 0.0001633; 94.02% below 1%.
- Foreground: 81643 samples; median relative depth error 0.0001936; 98.04% below 1%.
Includes edges/disocclusions; not a perceptual quality score. No offsets were fitted.
Constant-buffer view is transposed; engine projection is row-major. The transform tested is
P_target * transpose(V_target_cb) * inverse(transpose(V_source_cb)) * inverse(P_source).

AfwD3D11 is now an isolated GPU reconstruction prototype, not connected to headset submission.
It implements two-source depth seeds, iterative refinement, held-surface stale tests, stencil
foreground selection, and explicit holes. Unlike D1's triangle mesh seed rasterization, this
prototype uses atomic depth splats; replay must establish whether that tradeoff is acceptable.
It imports no UE3 scale, projection convention or foreground threshold.
GPU identity and two-source identity tests pass; stale epochs and singular matrices are refused.
Single-source real-scene replay leaves 31.64% holes after fixing sky clipping. Much is the native
canted-eye view's non-overlapping boundary, with foreground disocclusions also visible. This
is deliberately not hidden by copying unwarped pixels. Held-history replay is the next check.
Evidence visualization: local/sim-dlss-inputs/pair-warp.png (game-derived, never committed).

Actual TAA disassembly shows exposure/compressed-HDR handling, forward colour transformation,
temporal filtering and an inverse transformation. cb_taaamount=0 does NOT disable history blending.
Do not enable both TAA and DLSS, or replace this pass without checking its colour/exposure contract.
The NGX SDK header explicitly forbids deferred contexts (nvsdk_ngx.h lines 516 and 656).
A production adapter must run evaluation on the immediate execution lane and preserve D3D state.
## Two-source replay and simulator checkpoint
Native triple 16783/16784/16785 starts on the right eye, giving a real previous right
history, fresh left, and native right ground truth from the same synchronized tick.
Two-source reconstruction leaves 3.9483% uncovered pixels. With explicitly verified
stationary foreground reuse, this falls to 0.101269%; this option remains disabled by default.
Moving head/controller triple 84071/84072/84073 uses the same ordering. Without assuming
stationary foreground, 4.191776% remains uncovered: foreground 25.2456%, world 0.2939%,
sky 0.0995%. Covered-pixel median log-colour error is 0.0042383. These are replay diagnostics,
not a headset quality score or an FPS measurement. Moving foreground requires verified
per-frame controller transforms and a disocclusion strategy before production integration.
The simulator was used to launch and load the existing save, capture both eyes and movement,
and quit cleanly. Process-local simulator override files, VR settings and game configuration
were restored from the session backup; diagnostic logs/captures remain under local/.
The installed DLL is still diagnostic/native stereo. DLSS and AFW backends are compiled
prototypes; production adapter, menu, lifecycle validation and performance proof remain pending.
## Performance investigation, 2026-10-28
Test hardware: RTX 5090. Existing Edge of the World balcony save, fixed mock head and
controllers, 3072x3264 headset swapchains, 3286x3555 native scene buffers, runtime pacing
240 Hz and configured engine ceiling 300 FPS. No screenshot/readback during timed windows.
Simulator throughput is not a headset/compositor performance measurement.

Same-session native / alternating / native counter comparison (about 28-29 seconds each):

| Mode | Game Presents/s | Runtime-accepted submissions/s | Accepted native pairs/s |
| --- | ---: | ---: | ---: |
| Native synchronized, first | 117.24 | 58.60 | 58.60 |
| Alternating, limiter fixed | 97.77 | 97.77 | 0 |
| Native synchronized, return | 116.72 | 58.38 | 58.38 |

Alternating renders only one fresh eye per submission and does NOT reconstruct the held eye.
These numbers establish a performance opportunity, not an AFW speedup or equivalent image quality.
No rejected XR submission or dropped native pair appeared in these sampled windows. Real
headset reprojection/drop counters and VRAM use are not measured by this simulator experiment.

Found and applied: the explicit VR frame limit was owned only in synchronized stereo. Switching
to alternating restored the flat game's 90 FPS cap. StereoSync now owns the configured limit in
both stereo modes. Live validation: 300 remained owned after switching to alternating, and 90
was restored on switching to mono. Native stereo stays the installed default.
Installed, live-tested candidate SHA256:
6A031D4D09DF81B43CE587B42D9149D2CD1BAEAE25DE66A1010E874005DA2A6A.
Later diagnostic-only hand-world metadata changes compile but have not yet been run in game.

Measurement corrections: old xr_frames counts submission attempts. New xr_accepted counts
successful xrEndFrame calls; xr_native_pairs_accepted separates successful synchronized pairs.
Runtime acceptance alone does not prove scanout, absence of reprojection, or visual correctness.
Opt-in vr gpulisttiming TAG SECONDS records GPU timestamp spans around ExecuteCommandList,
CPU call durations, and Present bins. It excludes immediate-context work and gaps between lists;
it is not whole-frame GPU time. Readback is nonblocking; dropped and invalid queries are reported.

Exploratory full-resolution versus 0.67-scale native runs: command-list GPU mean 5.965 versus
4.283 ms per Present; observed Present rates 120.15 versus 131.69. A later full-resolution repeat
reached 139.76 Presents/s and 4.577 ms, so this sequence alone cannot attribute a stable FPS gain
to resolution reduction. Treat CPU/scene variation as unresolved; do not claim DLSS speedup.
The AFW isolated two-source replay GPU median was 0.494752 ms, p95 0.507584 ms over 60 samples
at 3286x3555. Upload, shader compilation and readback are excluded. This omits scene rendering,
DLSS, transfers, submission and concurrent workload; it must never be converted into game FPS.
Evidence: local/perf-ab, scripts tools/summarize_gpu_lists.py and summarize_heartbeat_rates.py.

## Motion convention validation
Moving same-eye captures 84071 and 84073: 448851 depth-consistent world samples. The captured
normalized motion requires scales (-width,-height) to map current to previous image coordinates.
Median error against camera/depth geometry is 0.395684 pixels before removing raster jitter,
and 0.005371 pixels (p95 0.014082) after removing it. Thus this capture's motion vectors exclude
raster jitter. Projection entries [0,2]/[1,2] contain twice the native jitter with clip W=-Z;
top-left raster pixel offset is (-nativeJitterX*width,+nativeJitterY*height). No fitted offsets.
This validates world camera motion for this capture, not all moving-object/weapon motion or
DLSS temporal convergence. tools/check_temporal_motion.py reproduces the comparison.
Matched captures now record serial-specific XR head/eyes/grips and pose timestamps. The next
compiled diagnostic additionally records controller-to-world matrices through the same
ControllerPoseAt convention used for posing weapons, with that serial's head axis and input.
Do not substitute latest controller poses or treat lost/held tracking as fresh.
Final checks for this checkpoint: full Release build passes; AFW GPU identity/two-source/refusal
checks pass; all 15 temporal contracts pass; DLSS GPU smoke passes all five modes and repeated
shutdown. A smoke invocation using relative SDK paths initially failed capability discovery;
normalizing runtime/cache directories to absolute paths fixed it, and the same relative-path
invocation now passes. Simulator overrides, VR ini and game config were restored after clean quit.
Native stereo remains default. No production AFW/DLSS menu is exposed yet.

## Live DLAA diagnostic checkpoint (2026-10-28)

Native D3D11 DLSS now evaluates inside the running D2 pipeline, using pre-TAA HDR
colour and per-eye histories, then replacing the temporal result before later postprocessing.
The original TAA dispatch/history update is deliberately retained for fallback health.
This is a diagnostic DLAA path, not the completed AFW/upscaling renderer.

TemporalBoundary splits deferred recording, retaining prefixes on command-list private
interfaces and replaying them in order on the immediate context. DLSS runs in an isolated
D3D11.1 context state, restoring the engine's entire prior state afterward. GPU tests pass
for order, retained bindings, toggle drain, state restoration, both eyes, and depth recorded
after TAA but executed before it.

First live attempts correctly fell back: CPU workers record TAA before the matching depth
downsample on a different deferred context. The capture probe demonstrated this ordering.
Depth is now registered with the command list that references it and accepted only after that
list executes, with exact serial/eye/epoch matching. No "latest depth" substitution.
The final live status recorded 42,083 successful evaluations, zero fallback/missing-depth
frames and zero boundary refusals. Disable/re-enable worked; simulated session loss and
return to 1280x960 then 3286x3555 recovered. An intended motion-input edit initially selected
the session-loss field; that capture is not motion evidence. The corrected head-sway/hand-jitter
capture is DVR_dlss_motion2. Static captures cannot establish temporal stability or headset comfort.

Matched same-session native/DLAA/native, RTX 5090, 3286x3555, cap 300, simulator pace 240:
| Metric | Native A | DLAA diagnostic | Native B |
|---|---:|---:|---:|
| Accepted native stereo pairs/s | 58.3194 | 56.2225 | 58.3858 |
| Presents/s | 116.7093 | 112.5509 | 116.8069 |
| GPU command-list mean ms/Present | 6.0860 | 7.1024 | 6.0488 |
| CPU ExecuteCommandList mean ms/Present | 0.0887 | 0.3917 | 0.0888 |
| Process private memory MiB | 9564.8 | 12120.2 | 9631.2 |
| Process working set MiB | 4507.2 | 4522.0 | 4570.9 |

These CPU spans are not total CPU frame time; GPU spans exclude inter-list gaps and other
immediate work. Native stereo uses two Presents per pair. Query instrumentation is enabled
for all three intervals. No dropped-pair delta; no runtime rejection in measured intervals.
This is approximately 3.65% slower, not a performance success. No second scene render has
been removed yet. Allocation/copy overhead and retained TAA remain in this diagnostic.
Private process memory is not VRAM; VRAM/compositor drops still need measurement.

Evidence is in local/boundary-check (CSV, timestamped heartbeats, captures, logs, backups).
Live-tested DLL SHA256: 6D2A7E0704714DCF9102E44438F8C1CBA270D18FBE8FA65A650815AA7CEC36EB.
Official pinned nvngx_dlss.dll installed beside it. Native remains default; vr dlsslive 1
is a temporary diagnostic command, not a saved menu renderer.
Game closed normally; simulator overrides removed, original VR ini and game config restored.
Remaining: actual reduced render resolution with full-resolution output/UI, temporal-pass
replacement with correct fallback, pooled resources, stronger cut/lifecycle validation,
AFW foreground motion/reconstruction/submission, menu, and honest performance/headset validation.

## Engine scaling investigation (2026-10-28)

A diagnostic-only vr dynamicresprobe command temporarily owns rs_enable,
rs_forceFractionX/Y, rs_useHwUpsample and rs_skipUpsample, restoring numeric values on exit.
It is mutually excluded from the DLAA diagnostic. It does not expose a fake DLSS mode.

Confirmed actual D2 behaviour: scene targets keep full 3286x3555 allocation, but the active
viewport becomes 2191x2370 at 2/3 and 1095x1185 at 1/3. Main TAA dispatch changes from
206x223 groups to 137x149 and 69x75. Final output remains 3286x3555.
Post-tonemap shader 7f17d0b9bf1fea47 writes reduced viewport into R8G8B8A8_TYPELESS.
The final ec1204b254163299 shader reads that resource at t0 (ro_identity_bufferin), uses
PerViewCB cb_resolutionscale (reflection offset 776), and writes the full-resolution output.
Actual SRV/RTV typed formats and exact per-frame scale/serial need the newly compiled
upscaleinputs audit; do not guess colour transfer or treat allocation size as active image size.
This is why simply writing a full-size DLSS result at TAA would be wrong: downstream
postprocessing and the final spatial upscaler still use the reduced viewport.

Same-scene scaling-only sequence (no DLSS), native/2/3/1/3/native, completed stereo pairs/s:
57.6954 / 57.8692 / 58.5606 / 57.3075.
Mean GPU command-list ms/Present: 6.1157 / 5.5870 / 5.2578 / 6.1265.
CPU ExecuteCommandList mean ms: 0.0794 / 0.0799 / 0.0794 / 0.0804.
Thus the 1/3 viewport reduces measured GPU work by ~14%, with only a small throughput
change. Other work limits this scene; this does not establish the exact CPU or GPU bottleneck.
No DLSS Quality-versus-Ultra-Performance claim can be made from this scaling-only test.
Evidence: local/resolution-check. One empty heartbeat snapshot during the native repeat
was discarded explicitly (file is rewritten non-atomically); remaining endpoint counters
are valid. Query instrumentation was present for every interval.

The live-tested DLAA DLL remains installed after restoration. Newer compile-only changes
add resource-reuse rejection for stale depth, diagnostic-mode exclusion, and upscale auditing.
Next: verify typed colour formats and rendered subrect; implement separate-eye upscaling
at the verified final boundary without feeding DLSS TAA-filtered input or distorting UI.

## 2026-10-28: per-eye SR diagnostic, live validation and negative performance result

The verified D2 final upscale boundary now runs DLSS SR for each eye independently.
TAA input colour is copied before temporal filtering; after its native history update,
the current unfiltered colour replaces its result. Tone mapping and reduced-resolution
post effects run next. At final shader ec1204b254163299, matching colour, depth and motion
are evaluated with the tone pass's actual active viewport. Full-size DLSS output replaces
the spatial upscale result. Both original TAA and spatial upscale costs remain for now.
This is a diagnostic integration, not the completed AFW renderer or a performance success.

The source and destination views at that boundary were audited as sRGB (format 29),
backed by typeless RGBA8 textures. Input is explicitly decoded through the sRGB SRV to
linear RGBA16F; output uses an sRGB RTV. The GPU test verifies linear 0.5 round-trips to
188 +/- 2 in all three channels, across all four SR quality modes and both eye histories.
The reduced-viewport motion capture (2191x2370) independently confirms negative active
width/height motion scaling: 220,480 world samples, jitter-corrected median error
0.001253 pixels, p95 0.006871. Evidence: local/upscale-check/cropped_sr_motion_*.

Live simulator candidate EA1164D15A392CCC975218954D4A6192041CC8057A7A720ABECFE847C87A991B:
Quality 2191x2370, Balanced 1906x2062, Performance 1643x1778, Ultra Performance 1095x1185;
all produce 3286x3555 before headset cropping. All modes evaluated both eyes with zero
recorded fallback/missing-depth counters. Captures show world and weapons at expected
scale; they do not establish headset clarity or motion quality.

Clean same-scene native -> Quality -> Balanced -> Performance -> Ultra -> native sweep:

| Mode | Accepted stereo pairs/s | GPU list ms/Present | CPU Execute ms/Present |
|---|---:|---:|---:|
| Native before | 57.024 | 6.143 | 0.090 |
| Quality | 51.260 | 6.956 | 0.463 |
| Balanced | 51.388 | 6.818 | 0.463 |
| Performance | 50.963 | 7.029 | 0.482 |
| Ultra Performance | 52.260 | 6.748 | 0.453 |
| Native after | 57.078 | 6.104 | 0.089 |

Evidence: local/sr-live-check/clean-results.json, sr_clean_*.jsonl and matching GPU CSVs.
The earlier sweep overlapped a synthetic GPU test and is excluded from performance claims.
Clean windows used simulator pace 240 and engine cap 300; captures occurred after timing.
GPU timings sum command-list execution including injected DLSS, not total frame GPU time;
CPU timings measure ExecuteCommandList calls, not total CPU frame time. Runtime acceptance
is not physical headset scanout. No dropped-pair increments were observed in clean windows.

SR is currently slower than native; Ultra is only about 2% faster than Quality here.
This does not prove a single CPU bottleneck: the adapter adds GPU work, state swaps,
allocations/copies and preserves redundant passes. Those costs must be isolated and removed.
AFW still has not replaced the second scene render in live submission.

Failure handling now disables SR on failed processing and releases its temporary scaling
ownership on Present. Negative GPU tests refuse missing or prior-epoch temporal packets.
A subsequent guard also detects two prepared frames without a successful final upscale.
Preset 0 (runtime default) versus explicit K (11) is a separate diagnostic comparison,
motivated by actual D1 staging dlss_host.cpp:477-492, not an assumed transferable speedup.
Native stereo remains the default and no production AFW menu is exposed.

Explicit preset comparison (same Ultra Performance quality and output, default/K/default):
52.032 / 51.831 / 52.152 accepted native stereo pairs/s. No measured improvement from K;
leave default unchanged. Evidence local/sr-live-check/preset-results.json and captures.
The runtime accepted some additional keep-alive frames; these are excluded by using the
completed native-pair counter, not total accepted XR frames. The preset field deliberately
reports the requested value; it does not claim proof of the runtime's internal selected model.

Candidate B38589C4FF46F3C315C3D7438E518A7B00D7C38354BA11043FFAFF640DA1B581 also ran
simulated head sway (input 50=3) and hand jitter (51=2), then restored both to zero and
returned to native stereo. All successful-evaluation counters remained matched with zero
fallback/missing-depth counters. A still capture is not a temporal quality assessment.

The next candidate reuses a single immediate-context linear colour workspace across
DLSS evaluations, recreating it only for configuration changes. GPU command ordering
serializes its use; both eyes retain distinct NGX feature histories. Expanded synthetic
tests use different eye colours (right-eye expected sRGB 99,188,137 +/- 2), exercise
all SR quality changes, and verify missing-final-pass and prior-epoch input refusal.
Full Release build, live-adapter GPU tests, all 15 temporal contracts and deferred-boundary
tests pass. No compiled-only change is treated as a measured performance improvement.

Workspace reuse candidate D4422BFEA1D42814BB8A1641A40356215CD46AE08AED66B96E220F84A03DABF4
was built, GPU-tested, installed and run in the simulator. Native/Quality/Ultra/native:
56.720 / 55.162 / 52.311 / 56.583 accepted stereo pairs/s. GPU command-list means:
6.145 / 6.827 / 6.630 / 6.160 ms per Present. CPU Execute means:
0.081 / 0.378 / 0.365 / 0.091 ms per Present. This reduces observed CPU execution overhead
relative to earlier SR measurements, but is not an isolated randomized A/B proof of every
FPS difference. Quality still does not beat native; Ultra remains slower in this run.
Do not infer an SR gain, a fixed bottleneck, or AFW success from these numbers.
4,559 successful evaluations, zero fallback/missing-depth counters; disable returned to native.
Evidence: local/sr-live-check/pool-results.json, pool-live.log and sr_pool_* captures/timings.

Session ended with the game closed. The original DLL, INI, game config and absence of mock
runtime/input overrides from local/sr-live-check/state.json were restored. The tested new
candidate remains in build-q/bin/DishonoredVR.dll and local/sr-live-check/DishonoredVR-pool-validated.dll.
Outstanding: pool remaining capture resources, safely replace native TAA/spatial upscale,
measure complete GPU/CPU frame costs, implement live AFW with correct held-eye foreground
and submission, VR menu/persistence, camera cuts/loading/resizing lifecycle and headset QA.

## 2026-10-28: PureDark/RE4 AFW source research (user-expanded reference scope)

The user explicitly expanded the external reference scope to PureDark and RE4VR after
questioning the negative DLSS performance result. No bbport/HZD code was used.
No DLSS/AFW SKILL.md was found in the installed skill roots, Mod Tools, or D1 staging
reference; the supplied implementation documents remain the technical references.

Actual primary source inspected, pinned rather than relying on release summaries:
- PureDark/REFramework RE4AFW_CPPVRMod at 5f064ac0d424cd86fde3e637d9216e07a9c89116.
- Also checked RE4RAFW VR.cpp and D3D12Component.cpp at 6f4dc46a37d6dfb1b410519d7952cf2f214c6c0a.
- https://github.com/PureDark/REFramework/blob/5f064ac0d424cd86fde3e637d9216e07a9c89116/src/mods/VR.cpp#L4050
  AFW returns before the second render sequence. The sequential path reaches the
  WaitRendering-to-EndRendering chain at lines 4163-4250. Camera and runtime pose updates
  explicitly run every AFW frame (3917-3952). This is the important removed work.
- https://github.com/PureDark/REFramework/blob/5f064ac0d424cd86fde3e637d9216e07a9c89116/src/mods/vr/D3D12Component.cpp#L169
  The fresh eye alternates. Current colour/depth/motion plus matching camera data go to
  EvaluateFrameWarp (174-250); both eye images are copied/submitted and OpenXR end_frame
  runs on every AFW frame (280-289, 517-559). Descriptor creation is conditional on resource
  changes. The implementation still contains command-resource waits; it is not evidence
  that arbitrary waits/copies are free. Those engine-specific mechanisms are not ported.
- https://github.com/PureDark/REFramework/blob/5f064ac0d424cd86fde3e637d9216e07a9c89116/dependencies/pd-afwmod/include/PDAFWPlugin.h
  The API provides AlternateEyeWarping, PreviousFrameWarping and CombinedWarping. Inputs
  are frame buffers, matrices and motion conventions; no DLSS feature is an AFW API input.
  The integration explicitly distinguishes same-eye from other-eye motion vectors.
  This supports an implementation dependency on upscaler plumbing, not an algorithmic
  requirement for NVIDIA DLSS. Do not transplant RE's motion scaling into D2.
- https://github.com/PureDark/REFramework/blob/5f064ac0d424cd86fde3e637d9216e07a9c89116/dependencies/pd-afwmod/dummy/PDAFWPlugin.cpp
  This is explicitly a stub; the real warp shader/backend is not published here.
  No claim is made to have inspected or decompiled that proprietary implementation.
- https://github.com/PureDark/REFramework/blob/5f064ac0d424cd86fde3e637d9216e07a9c89116/src/mods/TemporalUpscaler.cpp#L1336
  Native TAA is disabled when the replacement upscaler is active unless explicitly allowed.
  Separate eye upscaler instances and jitter histories are present. Source resource copies
  are recreated on resource changes, not blindly on every frame.
- https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md#70-add-dlss-to-the-rendering-pipeline
  NVIDIA's integration example selects DLSS OR the default upscale pass and describes
  distinct viewport identities and persistent feature resources for multiple views.

D2 diagnosis supported by source (not yet a fully isolated causal timing breakdown):
1. Current diagnostic still executes the native TAA dispatch/history copy and final
   spatial upscale, then replaces their output. It also performs full-allocation colour/
   motion copies and depth decode even when the active scene viewport is much smaller.
2. It still renders two scenes per completed stereo pair. No live AFW reconstruction
   exists in VRRuntime/XRSession. Therefore these are DLSS-on-two-renders experiments,
   not tests of the performance mechanism in PureDark's AFW.
3. DLSS can lose throughput when its cost plus integration overhead exceeds the work
   removed by lower resolution. Resolution reduction does not remove CPU scene work,
   every full-output post effect, or the redundant work retained by this adapter.
4. The transferable AFW budget is one fresh scene plus one fresh DLSS evaluation plus
   one held-eye reconstruction per submission. Compare that with two complete scenes
   and two DLSS evaluations. Use actual stage costs; do not promise 60-80 percent.

Implementation priorities: measure stage costs; replace rather than retain temporal and
spatial passes with fail-safe recovery; pool/crop capture resources by valid viewport;
connect one-render stereo submission with current held-eye pose and reconstruction;
validate moving foreground/disocclusions and reset history at lifecycle changes.
Do not report doubled AER submission counters as an AFW gain without reconstructed eyes.

Live stage profiling: Release candidate C74E6930D21E05FCEF37D4CD754377B71DE0AC45447F9DBE0656555CF32F26F1.
Evidence: local/afw-research-profile; same saved balcony scene, simulator, output
3286x3555 per eye. Each mode warmed up five seconds, sampled twenty seconds; the
first three seconds of samples were excluded. Quality: 106 preparation samples,
107 final samples; Ultra: 108/107. Both eyes represented, zero dropped/invalid queries.
Mean preparation/final span GPU time: Quality 0.0788/1.2976 ms per eye; Ultra
0.0552/1.3358 ms per eye. Combined sampled means 1.3764/1.3909 ms (different stage
samples, not an exact whole-frame measurement). CPU recording means 0.1482/0.1101
and 0.1564/0.1063 ms. These exclude deferred capture copies, original engine passes,
whole-scene CPU/GPU time, and XR submission. Nested timestamps around NGX returned
implausibly small 0.004/~0 ms despite the wider stage cost; they must NOT be used
to claim DLSS itself is free or attribute all cost to conversion. Driver scheduling
or internal submission needs further investigation before that subdivision is valid.
Measured accepted completed stereo pairs: Quality 52.78/s, Ultra 53.26/s, zero dropped
pairs. This is an instrumented diagnostic, no concurrent native baseline; it does
not establish a performance improvement over native or a robust mode advantage.

Added opt-in vr dlssprofile TAG seconds: asynchronous DONOTFLUSH timestamp polling,
every seventeenth callback, bounded query slots, per-eye/stage CSV, diagnostics.
Release build, dedicated hardware D3D11 stage profiler smoke (23 samples), and
existing live DLSS smoke passed. No live AFW or performance optimization is claimed
from this instrumentation change. Restore completed after normal game exit: previous
installed DLL 6D2A7E0704714DCF9102E44438F8C1CBA270D18FBE8FA65A650815AA7CEC36EB,
original INI/game config and absence of simulator override files restored. Candidate
retained under local/afw-research-profile/DishonoredVR-profile-validated.dll.

## 2026-10-28: actual user-supplied PureDark plugin compatibility

User supplied E:/Program Files/Steam/steamapps/common/RESIDENT EVIL 4  BIOHAZARD RE4/PDAFWPlugin.dll.
Its SHA256 is 48D5849F64AB1B9AB2717247562AFC203CD92900C901510983C745618CC9E5D5;
the installed Q: RE4 copy has the same hash. It exports InitDevice, InitFrameWarp,
EvaluateFrameWarp. Read-only export/disassembly inspection confirms InitDevice requires
a non-null D3D12 queue (DeviceParams offset 24); merely providing D3D11 device/context
cannot initialize this binary. It is not the dummy implementation. No DLL was modified,
copied into D2, or added to Git.

Implemented tools/puredark_afw_probe.cpp as an optional standalone executable, enabled
with DVR_PDAFW_INCLUDE pointing at the pinned RE4 public header directory. The local
header is from RE4AFW_CPPVRMod 5f064ac0d424cd86fde3e637d9216e07a9c89116,
dependencies/pd-afwmod/include/PDAFWPlugin.h. In the probe-only local header the glm
include is replaced with a 16-float default-layout mat4; ABI size assertions verify
DeviceParams 40, TextureDesc 56, EyeFrameBuffers 336, CameraData 768 bytes.
This is a narrowly tested ABI, not permission to assume other plugin versions match.

The probe creates D3D11 and D3D12 devices on the same hardware adapter. A D3D11-written
shared input texture and a shared fence feed the real D3D12 plugin. Its output is copied
to a shared texture, fenced back to D3D11 and read there. Identity and a known +4-pixel
clip translation each passed both eye indices: 3136/3136 interior pixels per case.
The pattern varies with x/y and eye, so a plain unchanged copy fails the translation
check. All calls and GPU completion succeeded; device-removal check passed. The optional
D3D12 debug layer is not installed on this machine, so no debug-layer validation claim.
Evidence: local/puredark/probe-result.txt, local/puredark/PDAFWPlugin.h,
local/puredark-re4-disasm.txt. DLL remains in its user-supplied location.

This proves a real plugin interop route, not a completed D2 integration or AFW speedup.
Probe-only CPU waits/readback and allocation per iteration must not be copied into the
live path. Still required: persistent D3D11/12 bridge resources and bounded in-flight
fence ownership; D2-matched colour/depth/motion with verified source and target matrices;
current-frame held-eye pose; skip native second render and submit both outputs every
frame; lifecycle reset/fallback; HUD/foreground/motion/capture tests and performance A/B.
The actual plugin is now available; the previously missing-backend limitation is resolved.

## 2026-10-28: actual PureDark backend integrated and measured in D2

The user-supplied PDAFWPlugin.dll is now called in the live D2 path, using a
same-adapter D3D12 queue and persistent D3D11/D3D12 shared textures/fences.
The backend is optional at build time (local PDAFWPlugin.h ABI header) and the
proprietary DLL is not redistributed in the repository. SHA256 of the supplied DLL:
48D5849F64AB1B9AB2717247562AFC203CD92900C901510983C745618CC9E5D5.

AfwLive matches source view serial, eye, DLSS evaluation, depth, motion and epoch.
ViewMatrixHook constructs the opposite-eye view from the same current pose generation;
this is a matrix calculation, not an additional scene draw. The alternate-eye scheduler
runs one fresh scene each tick, evaluates that eye's own DLSS history, reconstructs the
other eye, and submits both with current-frame poses. Native stereo remains the fallback.

PureDark CombinedWarping fills holes with temporal history. The initial fresh-only mode
had obvious gaps and was rejected. Preserve the output sRGB format: treating those bytes
as linear both washed out the held eye and caused the submission blitter to reallocate
when alternating between differently formatted eyes. R32 shared textures require the
D3D12 simultaneous-access flag. Three fenced command slots prevent allocator reuse while
in flight; no CPU fence waits or readbacks are in the live reconstruction path.

AfwInputs converts the verified active depth/MV viewport into full output dimensions.
Point samples preserve depth edges and normalized MV units. D2's derived raster jitter
is (-jx*renderWidth,+jy*renderHeight); matching jitter is removed from the projection to
align depth with DLSS's unjittered colour. This is engine-derived, not copied from RE4.
The companion AFW guide's same-frame camera/depth requirements and the DLSS document's
per-eye history/active-viewport rules were cross-checked; engine addresses/formats were
not transplanted. DLSS still executes the native TAA pass for fallback history, then
uses unfiltered inputs for its own temporal resolve: redundant cost remains, rather than
feeding the native temporally-filtered result through DLSS again.

### Reproducible simulator results

Scene: Edge of the World balcony save, sword/crossbow visible, stationary mock head and
hands, same graphics and save, no captures during timing. Mock recommendation 3072x3264;
D2 full per-eye output 3286x3555. Engine cap 300, mock pace 240. Rate is accepted completed
stereo submissions divided by elapsed wall time, NOT game Presents or the game's FPS.
First run brackets: native 58.04 / 57.48; AFW+DLAA 93.00; native+DLAA 52.87.
That isolates the AFW benefit from DLAA: DLAA alone did not produce the gain.

Converted-input candidate SHA256:
5D5D466663C7CDB3E76F6FFAFF5159B6DCB024BCA89BF8A70A0C801314A83E69.

| Mode | Completed stereo FPS | Wall ms / pair | Fresh scene dispatches / accepted pairs |
|---|---:|---:|---:|
| Native stereo | 57.89 | 17.27 | 1877 / 939 |
| AFW + DLAA | 92.04 | 10.86 | 1494 / 1494 |
| AFW + Quality | 87.90 | 11.38 | 1422 / 1422 |
| AFW + Balanced | 87.31 | 11.45 | 1415 / 1415 |
| AFW + Performance | 88.17 | 11.34 | 1428 / 1428 |
| AFW + Ultra Performance | 87.23 | 11.46 | 1411 / 1411 |

Every AFW measured interval has presents = identified TAA scene dispatches = fresh AFW
inputs = warp calls = accepted stereo pairs, zero held simulation ticks, zero AFW refusals.
Native has about two scene dispatches per completed pair and one held simulation tick.
The one-count native boundary difference comes from sampling an interval mid-pair.
DLAA is ~59% faster than native in this run. Upscaling modes do not beat DLAA here, despite
verified actual input viewports (Quality 2191x2370, Balanced 1906x2062). Fixed output-size
work, bridge/copies and the late SR path remain; this result is not evidence that SR
inherently lowers FPS. No claim about which bottleneck dominates without further profiling.

Raw evidence: local/afw-live-check/{converted_mode_*.jsonl,converted-results.json,
converted-modes.log,results.json,combined-benchmark.log}; scripts measure-converted.ps1,
analyze-converted.py. First measured candidate preserved as measured-dlaa-candidate.dll.
Process private memory is recorded (~9.55 GB native / 13.5-14.1 GB AFW); it is NOT VRAM.
The retained opaque plugin affects post-AFW native memory. Wall ms/pair is NOT GPU time.
Mock OpenXR cannot establish real compositor reprojection, headset comfort or latency.
Separate whole-frame CPU/GPU time, reconstruction GPU cost and local VRAM still need a
validated profiler capture. Do not label the existing gpu_present_interval as GPU render time.

### Validation and limitations

Release build passed. afw_inputs_smoke verifies active viewport crop, depth-edge position,
jitter correction, unchanged normalized motion, state restoration, reuse and invalid-input
refusal. puredark_backend_smoke verifies the real DLL with repeated alternating frames,
known camera translation, sRGB sharing and invalid-eye refusal. dlss_live_smoke passes DLAA
and every SR quality, both eyes, deferred ordering, reversed depth recording order, sRGB
roundtrip, state restoration, missing/stale-frame fallback.

Live mode cycling and a 12-degree head sway / 4-degree controller jitter run stayed active.
Both-eye captures inspected; large first-build gaps/brightness mismatch resolved. These
are not a headset acceptance, nor proof that all disocclusion/foreground motion is correct.
The user's alignment screenshot is a qualitative target, not justification for arbitrary
camera offsets. Output resizing currently falls back to native and requires a backend
restart; the opaque plugin has no shutdown API, so its resources remain until process exit.
ClearBeforeWarping is used on epoch/serial discontinuity, but full opaque-history reset on
camera cuts/loading is not established. Native fallback is intentional on mismatched data.

Controller menu integration adds Native Stereo / AFW + DLSS and DLAA through Ultra
Performance, saving Display.Renderer 0..5 in DishonoredVR.ini. No unsupported sharpening or
model controls are exposed. Menu/fallback changes made after the above benchmark require
separate live validation; do not attribute the table to an untested later binary.

Post-benchmark lifecycle checks (candidate 574EC7CB...): output scale changed from 1.0 to
0.9; AFW refused the incompatible output and returned to native stereo at 2957x3199.
The failure reason remained visible. Evidence: lifecycle.log and menu-candidate-resize.txt.
A menu-open activation initially failed matching-frame checks; this was found during
controller testing, not concealed as success. Subsequent code waits while Overlay is
visible, resumes validation after B closes it, and resets the consecutive-miss counter.
The capture command now optionally saves the separate VR-menu texture for visual QA.
Native bracketing measurement after the full mode sweep: 56.96 stereo FPS / 17.56 ms,
1618 scene dispatches for 809 accepted native pairs. Raw: converted_native_b.jsonl.

VR menu QA found that one XR stick movement could skip several controls. The menu was
sending Gamepad events while the Win32 ImGui backend also polls XInput; these are two
input producers. OpenXR now drives ImGui keyboard navigation (arrows/Enter), with
ImGui's gamepad navigation disabled, so XInput cannot independently navigate the menu.
Dependency checked: installed ImGui 1.92.8; official backend source:
https://github.com/ocornut/imgui/blob/v1.92.8/backends/imgui_impl_win32.cpp#L316
The menu's old raw Present-rate FPS label was removed; it did not measure stereo FPS.
The renderer control receives default focus when the menu appears.

Menu activation verified with captured menu and saved INI: Renderer=1 (AFW+DLAA), then
B closed the menu and reconstruction reached fresh=rebuilt=accepted=621, with only two
transition refusals before the valid scene. Explicitly disabling DLSS after successful
AFW activation exercised the real missing-input fallback (missing-dlss-pass.txt).
The keyboard activation mapping uses Space (ImGui's activate action), not Enter (text
input preference). This distinction was verified against the live dropdown.
The controller-only final restart check is recorded separately below.

Final controller/restart validation (AE54EFFA0C941EDBA2B0C5EBDB692D4902449F71A989C813FFAE3DFB7F887CF8):
- INI Renderer=1 read on restart; package issued vr afw 1. After entering gameplay,
  fresh=rebuilt=accepted=533, refused=0 (restart-activation.txt).
- Controller stick navigated to quality, controller A opened all five choices, controller
  A selected Quality, and INI became Renderer=2. B closed the menu; fresh=rebuilt=accepted
  reached 1260. Two transition refusals occurred; no fallback (controller-quality-pass.txt).
- Both menu captures saved and inspected: DVR_controllerquality_menu.png and
  DVR_controllerselected_menu.png. No mouse was needed for this final test.
- Build passes; native output-resize and missing-DLSS fallbacks verified. Camera-cut and
  loading history acceptance, real headset stereo comfort, full CPU/GPU/VRAM profiling
  remain open. This is an experimental candidate, not a release acceptance.

Final exact-candidate A/B/A: native 57.5036 -> AFW+DLAA 92.2225 -> native 57.8678 completed
stereo FPS (13.16-13.18-second analysed intervals after warmup). AFW 10.8433 wall ms/pair;
native 17.3902 / 17.2808. AFW counters: 1214 Presents, 1214 identified scene TAA dispatches,
1214 fresh inputs, 1214 warp evaluations, 1214 accepted stereo submissions, zero refusals,
zero held simulation ticks. This is +59.87% versus the mean of the native brackets in
this one scene. It is not a promise of a fixed percentage in other scenes or on a headset.
Tracked summary: docs/results/dlss-afw-simulator-2026-10-28.json.

Deployment: candidate AE54EFFA... and user-supplied PDAFWPlugin.dll installed at
Q:/SteamLibrary/steamapps/common/Dishonored2. Game exited normally. Original INI and game
configuration restored; simulator override/input/HMD files removed. Native Stereo is the
default (missing Display.Renderer defaults to 0). Hold X for 3 seconds, select Renderer
AFW + DLSS, keep DLAA initially, then B to close the menu. Choice saves automatically.

Recovery baseline: local/afw-live-check/DishonoredVR.dll, SHA256
6D2A7E0704714DCF9102E44438F8C1CBA270D18FBE8FA65A650815AA7CEC36EB, with original INI and game
config in that directory. Stop the game before restoring. Tested final binary also saved
as local/afw-live-check/final-candidate.dll. No commits, pushes or proprietary DLL/header
redistribution were performed. Source checkpoint exported to local/dlss-afw-live-candidate-checkpoint.patch.

Profiling follow-up: see `FRAME-PROFILING-2026-10-28.md` for the newer installed candidate,
per-function measurements, repeated AFW counters, Toolkit compatibility findings and
restoration details. The earlier deployment hashes above describe the earlier candidate.

## Active-scene follow-up

The subsequent draw-level audit corrects the inference that SR left main-scene shading at full resolution: the engine's dynamic-resolution path already reduced geometry viewports and multiple dispatches. Full-size allocation dimensions were not shaded dimensions. See `DLSS-ACTIVE-SCENE-2026-10-28.md` for active-rectangle input copies/depth conversion, reduced draw-hook overhead, repeated quality-mode measurements and the latest candidate deployment.
