> Release context (2026-11-01): 0.7.0 packages the accepted work below with a verified offline installer/launcher. Installed hashes, local backups and experiment states below describe the October 31 development checkpoint, not the 0.7.0 release artifact. Raw logs, saves, private paths and proprietary source headers are not part of the source upload.

# Dishonored 2 VR — performance reference and development handover

**Updated: 31 October 2026.** This consolidates the startup, FPS, CPU, pacing, resource-reuse, fallback-recovery and combat-clarity work in this conversation. It is a historical reference, not authorization to launch games, change settings, reinstall diagnostics or resume experiments.

For a future chat, say: **“Read the Dishonored 2 VR Performance Reference and continue from its current installed state.”** Attach this file or give its path. This is an ordinary Markdown reference, not an automatically registered skill.

## 1. Current state — read this first

- The user requested removal of the new submission CPU diagnostic. It has been removed from **both the installed mod and active source**. Its code/results remain archived for reference only.
- Restored installed DLL SHA256: `43FCCCDFEDFA6B098524921226D2C1E8DC19706862CC48BA8150020A463356F3`.
- Restored PDB SHA256: `F5AE6A33568FCFA77BFF0A291F8761850D7865809FA7732300D5A03DA46A6F8D`.
- Accepted CPU defaults: guarded GameLocal singleton lookup **on**; transform-readiness optimization **mask 16**. The older experimental bits 1/2/4/8 remain off. Later bits 32/64/128 have been removed from the active renderer and archived.
- Existing lightweight heartbeat/wall timing remains; removal concerns the newly added calling-thread CPU probe, its automatic capture tail and `submission_diag` logging. Detailed per-draw profiling and motion statistics are not enabled for normal play.
- Graphics quality and the user's selected resolution were preserved. Saves/configuration were not altered by diagnostic removal. Earlier comfort changes intentionally suppress selected blur effects at the user's request.
- The SteamVR dispatch marker is present locally. That path is conditional on SteamVR and is **not used by VirtualDesktopXR**.
- The latest diagnostic-only build (`FB253B480C87475F461F819D179466335161AA8C9341EBB56859ECC0B7A4AD93`) is **no longer installed**. Do not restore it merely because an older report says “installed.”
- No controlled result establishes stable 90 FPS in all crowded areas or during headset turns. The squares/sky-smearing issue remains unresolved.

Repository: `<repository>` (working branch used: `codex/dlss-afw`). Game: `<game>`. Build tree: `build-q`. Do not substitute older C: checkouts. The worktree contains substantial pre-existing development changes; never blanket-reset it.

## 2. Accepted work and what it actually establishes

### Startup compatibility: audio-worker affinity

The repeated startup crash was traced to Wwise streaming/audio worker initialization failing with error 87, followed by a null dereference during engine cleanup. Requested audio-worker affinity was `FFFFFFFF`, while the permitted process mask was `FFFF` on the test machine.

The supported-executable/prologue-guarded hook intersects the worker's requested 32-bit affinity with the process's permitted mask. Zero preserves inheritance when no allowed CPUs can be represented. It does not change process affinity, pin the whole game, or alter the original settings object. Fixing initialization was preferable to hiding the cleanup crash. Startup then reached rendering. This is a compatibility fix, not an FPS claim.

Source: `src/Src/Main.cpp`. Evidence: `output/d2-startup-20261030/resolved-startup.txt` and `investigation.txt`.

### Small independent desktop window and less routine logging

The local mirror window was reduced from 1280×960 to 640×360, while RenderScale stayed 1 and headset render dimensions remained independent. Controller-input logging was disabled; status logging remained. No measured FPS gain is attributed to these settings alone. Do not shrink the headset render target when reproducing this cleanup.

### AA changes no longer silently destroy DLSS's required inputs

After an AA/settings change, earlier logs showed exhausted DLSS retries and permanent native-stereo fallback. Roughly 90 engine Presents/s then supplied about 45 native stereo pairs/s; this did not prove GPU saturation.

While DLSS is active, it now maintains the required temporal AA preset/pass. It remembers the latest differing native-AA preference and restores it when DLSS ends. A repaired AA change invalidates old temporal inputs and resets the recovery budget. Requested quality/resolution remains unchanged. GPU tests covered both eyes, DLAA/SR qualities, expected pixels, AA restoration, invalid guides and bounded recovery.

Source: `DlssLive.cpp`; tests: `dlss_live_smoke.cpp`. Evidence: `output/d2-aa-session-20261030-040331/fix-notes.txt`.

### SteamVR pacing/dispatch: correct lock ownership and no per-call task allocations

Wait/begin/end calls can share one persistent SteamVR worker. A previous trial stalled because the caller held `ID3D11Multithread` while the worker made runtime calls. The corrected keep-alive path acquires the D3D11 lock inside the worker that uses it.

The synchronous mailbox uses stack-owned callbacks instead of allocating `shared_ptr`/`packaged_task` work for each call. It is selected by the game-local `DVR_steamvr_frame_dispatch.txt` marker and the SteamVR runtime check. Other runtimes retain their direct path. Existing frame prefetching and VR desktop Present sync 0 should not be misrepresented as newly added by the later diagnostic work.

Dispatch concurrency, callback lock ownership and startup/menu/keep-alive lifecycle were tested. Crowded-scene FPS improvement was not established. Do not turn this into a blanket runtime change or remove D3D protection to chase timing.

Source: `XRSession.cpp`, `FrameDispatch` implementation. Evidence: `output/d2-perf-20261030/installed-notes.txt`.

### CPU overhead: shader lookup caching

Per-thread shader-hash caches avoid repeatedly taking the shared shader-map lock, including wrist-HUD vertex-shader lookup. Shader registration invalidates caches so reused shader addresses cannot retain stale identities after level loads. This accepted cache is distinct from the later, unaccepted experiment enlarging a layout cache from 8 to 64 entries.

No isolated percentage gain is established for this change. Keep invalidation and object-lifetime checks intact.

### Resource reuse: bounded private DLSS colour snapshots

Private HDR/post-tonemap colour snapshots use a bounded pool: eight slots, at most 128 MiB retained texture storage. Reuse requires both the end of the command-list batch lease and GPU completion. Polling does not flush or wait. Unsupported descriptors or pool exhaustion fall back to allocation. AFW-retained motion/depth resources were deliberately not pooled in this revision.

Actual D3D11 tests exercised ownership, completion, resizing/discard, both-eye DLSS output and 184 successful reuse hits. A real Quest/VD log later showed 23,832 hits and six misses, confirming reuse occurred. That is not an isolated FPS uplift estimate.

Source: `DlssLive.cpp` and colour-pool implementation/tests. Evidence: `output/d2-perf-20261030/installed-notes.txt`, `output/d2-quest-drop-20261030-045308/installed-recovery-notes.txt`.

### Persistent FPS drops: cheap temporal reset and bounded AFW recovery

In a real VirtualDesktopXR session, AFW ran around 89–91 accepted updates/s before a mantle transition triggered missing temporal inputs and permanent native fallback. The old build did not log the exact failed match field; AA cannot be asserted as the cause of that event.

Transient input loss now invalidates temporal epoch/history without unnecessarily shutting down NGX and rebuilding the backend. Existing stale/mismatched-input validation remains. If immediate retries still fall back, AFW can restore the selected quality after settled gameplay, outside menus/loading/scripted animations. Retry delays are 2, 5 and 15 seconds; three attempts, with the budget replenished after 120 accepted frames. Explicit renderer choices cancel recovery. Hard backend/image/adapter/frustum/matrix failures are not blindly retried.

A later fix explicitly classifies missing matching AFW frame data as recoverable, even when DLSS's transient flag is absent. A controlled menu-transition reproduction recovered automatically on the first settled-gameplay retry, then recorded 1,132 accepted AFW reconstructions without native pair counts continuing to rise. These fixes address permanent fallback; they do not guarantee that every underlying input mismatch or ordinary frame spike is gone.

Source: `DlssLive.cpp`, `AfwLive.cpp`, recovery policy/tests. Evidence: Quest-drop notes and `docs/CPU-CACHE-CANDIDATES-2026-10-31.md`; live recovery evidence in `output/d2-cpu-release-20261031-163307/`.

### Detection/combat colour-input resilience and blur suppression

An earlier combat session had matching temporal/final identity but no colour viewport record and eventually fell back near 44 stereo FPS. Tracking only one tonemap shader hash was too restrictive. Eligible fullscreen RGBA8 colour writes are now tracked across shader variants; only the original audited upscale shader can initiate DLSS evaluation. Up to eight colour-target records per view prevent an auxiliary effect output replacing the required source. Frame, epoch, eye, source identity, dimensions and actual viewport must still match. The original triggering shader was not captured, so the precise detection effect is unproven.

Enabled comfort settings are reasserted at the existing 100 ms poll during XR: `r_skipCameraMotionBlur=1`, `view_damageBlur=0`, `view_doubleVision=0`, and `r_skipDOF=1` when its option is enabled.

Additional supported-executable/signature/vtable/class/inactive-token-guarded hooks suppress optional dedicated motion-blur, combat-focus and heavy-hit screen-effect starts. Existing cleanup and gameplay timers remain. No global time scaling, power timing changes or blanket postprocess bypass were introduced. The user reported improvement, but elimination of every combat blur was not verified.

Source: `DlssLive.cpp`, `Settings.cpp`. Evidence: `output/d2-detection-20261031-160157/installed-notes.txt`, `output/d2-cpu-combat-20261031-162109/installed-notes.txt`.

### Remove unnecessary diagnostic work from hot paths

Wrist-HUD diagnostic timing is sampled once per 64 draws; `checkusEstimate` is an estimate, not an exact total. Object-motion statistics are opt-in (`vr motionstats 1`), off normally. This avoids diagnostic inverse/multiply work, viewport queries, projections and shared counter updates while leaving rendering correction/history/output matrices unchanged.

These are legitimate overhead reductions, but repeated simulator runs did not establish a consistent FPS uplift. Do not invent one. See `docs/SIMULATOR-CPU-2026-10-31.md`.

### Measured CPU improvement 1: guarded GameLocal singleton read

Sampling found repeated `VirtualQuery`/`NtQueryVirtualMemory` work through `Game::LocalPlayer`. The fixed singleton pointer slot for the supported executable now uses a fault-guarded read. It reads the current value each call, never caches a player/world object, retains supported-build rejection and preserves dynamic-object checks.

Interleaved fixed-view simulator measurements (2633×3428 per eye, AFW4, quality index 3, active tracked hands):

| Mode | Accepted FPS A / B | Mean delivery interval A / B | p95 A / B |
|---|---:|---:|---:|
| Original lookup | 92.480 / 93.131 | 10.783 / 10.722 ms | 12.452 / 12.393 ms |
| Guarded read | 99.927 / 99.872 | 10.013 / 10.008 ms | 11.691 / 11.701 ms |

Approximately **7.6% throughput improvement**, with about **0.74 ms** lower average delivery interval in that comparison. Zero new AFW refusals. These are wall delivery intervals, not CPU-only execution or Quest display timing. p95 still exceeded the 11.11 ms budget for 90 FPS.

Enabled by default. `vr cpusingleton 0|1` is an in-session comparison switch, not a persistent user setting. Source: `Game.cpp`. Evidence: `docs/CPU-THREAD-INVESTIGATION-2026-10-31.md`, `output/d2-etw-20261031/`.

### Measured CPU improvement 2: transform-readiness witness

CPU mask 16 adds an atomic serial witness to skip repeatedly locking `g_XfM` merely to check whether the exact view already has a completed valid transform. The witness is cleared under the writer lock before rebuilding and on reset, then release-published only after success. All actual transform data reads/writes retain their locks. Invalid/no-controller transforms do not publish readiness.

Heavier fixed-view simulator comparison (3286×3555 output, AFW4, quality index 3, render scale 1):

| Mode | Accepted FPS A / B | Mean interval A / B | p95 A / B | p99 A / B |
|---|---:|---:|---:|---:|
| Without readiness | 85.262 / 84.662 | 11.720 / 11.769 ms | 12.787 / 12.811 ms | 13.429 / 13.626 ms |
| Readiness enabled | 89.860 / 89.652 | 11.066 / 11.106 ms | 12.061 / 12.217 ms | 12.771 / 12.866 ms |

Approximately **5.6% throughput improvement** and **0.66 ms** lower mean interval in that view. Zero new AFW refusals. Tail intervals still miss 11.11 ms, so this is not stable 90. Do not add this percentage to the singleton percentage: they were measured under different conditions.

Default `vr cpufast 16`; `vr cpufast 0` temporarily returns to the pre-readiness path. Source: `GpuTrace.cpp`. Evidence: `docs/RENDER-CONTENTION-2026-10-31.md`, `output/d2-contention-20261031/`.

## 3. Experiments not accepted for normal play

| Candidate | Result / current disposition |
|---|---|
| Bit 1: larger per-thread shader-layout cache | Apparent gains did not repeat; off. |
| Bit 2: exact 16-float matrix-factorization cache | No controlled repeatable benefit established; off. Bitwise equality/collision tests exist. |
| Bit 4: context/view serial publication witness | No repeatable throughput benefit established; off. Command-list generation invalidation remains essential. |
| Bit 8: per-thread coherent render-view snapshot | No accepted gain alone; off. Combined results do not establish an independent benefit. |
| Bit 32: reject irrelevant shader layouts before buffer lookup | About 101.803 baseline versus 100.485 accepted simulator FPS in a bridge comparison; slower. Archived, removed from active renderer. |
| Bit 64: reuse per-view hand-transform snapshots | Early gains inconsistent; final street repeats slightly slower. Archived, removed from active renderer. |
| Bit 128: factor/reject unmovable matrices before snapshot access | Essentially tied overall; some worse spikes. Archived, removed from active renderer. |
| Bits 64+128 together | Street average near baseline but worse tails; not accepted. |
| Broad single-pass stereo / VRS / speculative async submission | No accepted implementation from this work. Historical handover says single-pass experiments were paused; do not resume automatically. |

Final street comparison of the three-pass experiments: baseline means 8.520/8.435 ms, snapshot reuse 8.596/8.498 ms, matrix preflight 8.555/8.443 ms. Combined mean 8.461 ms but p99 11.462 ms versus baseline p99 10.021/10.319 ms. All valid listed trials had zero new AFW refusals. Skyward trials were mostly tied around 142–145 accepted frames/s. Invalid movement-marked trials must be excluded.

Evidence: `docs/CPU-THREE-PASS-2026-10-31.md`, `output/d2-three-pass/RESULTS.md`, archived experimental source/DLL in the same folder. Their historical masks are not supported by the current restored renderer (active tool accepts 0–31).

## 4. Submission diagnostic — removed at user request

The real-headset evidence included about **8.110 ms mean xrEndFrame** and 4.742 ms outside-Present time in one heartbeat. The simulator instead measured roughly 0.02–0.03 ms submission. Low GPU-reported time therefore did not establish a purely CPU-compute bottleneck, nor did the screenshot prove a decoder fault.

A temporary diagnostics-only build paired calling-thread CPU time with matching submission wall samples, automatically armed after sustained >3 ms mean submission or a >11.11 ms call, retained eight quiet heartbeat windows, and logged FPS plus horizontal/vertical look direction. It passed six tests and simulator functional checks, but no real Quest/VD session was recorded with it. A fixed-view overhead comparison was invalidated by player movement. No performance gain or zero-overhead claim is justified.

**It is now uninstalled and removed from active source/test registration.** Only the archive remains at `output/d2-submission-diagnostic-20261031-200451/`, with `REMOVED.txt` identifying the current status. Do not install it again without a new request. Existing baseline wall timings are still available.

Interpretation lessons: Windows thread CPU accounting is coarse; per-call peaks are unreliable for tiny calls. On the SteamVR dispatcher path, a caller measurement covers the waiting caller, not worker CPU. Wall-minus-CPU does not by itself identify GPU blocking, runtime waiting or scheduler preemption. A simulator cannot validate a VD-specific wait fix it does not reproduce.

## 5. Visual issues and performance questions still open

- Direction-dependent crowded-area drops and stable 90 during headset movement remain unresolved.
- Two squares and sky bleeding/smearing into the middle distance were reported in the user's VD screenshot. Raw-eye simulator comparisons, including AFW versus native-eye submission, did not reproduce the severe artifacts. There is no verified fix; do not declare AFW, DLSS, timewarp or streaming cleared of responsibility.
- Combat-blur suppression is targeted and guarded; not all remaining visual blur is proven eliminated.
- Normal per-eye native rendering and reconstructed stereo have different work/submission relationships. Engine Presents, accepted AFW frames, stereo pairs and headset displayed FPS are not interchangeable.
- User screenshot: Quest 3 / VDXR, target 90, Godlike, HEVC 10-bit 200 Mbps, spacewarp off; 85 FPS, Game 5 ms and decoding 11 ms at that instant. These are observations, not proof of which stage caused the drop.
- Native TAA cost remains in the present DLSS path. Eliminating it would be separate correctness/performance research, not a proven free gain.

## 6. How to investigate without repeating failed work

1. Back up installed DLL/PDB, mod INI, engine config, current logs and save/profile files before testing. Preserve the dirty source baseline/diff. Never overwrite newer saves as routine rollback.
2. Read the runtime, renderer, quality, eye dimensions and fresh heartbeat before choosing a test. PSVR2/SteamVR and Quest/VDXR findings must be distinguished.
3. Require actual loaded gameplay, both valid controller poses, active hand transforms, fresh AFW frames and verified intended save/view. Startup, loading, menus, untracked hands and native fallback are not representative hand-render CPU benchmarks.
4. Use interleaved repeats in one scene/session. Keep graphics and view constant; collect multiple directions separately. Current benchmark rejects >15 cm movement or roughly 3° direction/pitch change, writes an invalid marker, and restores the prior mask in `finally`.
5. Warm up, exclude initial profiler seconds, and wait for asynchronous CSV output to finish. Never compile, run tests, copy large assets or parse large CSVs during timed trials. Separate sparse detailed profiling from throughput runs.
6. Report means and p95/p99/tails, AFW refusals/fallbacks, and limits. Do not sum overlapping worker durations into CPU critical-path time. A 90 FPS average is insufficient when long frames exceed 11.11 ms.
7. Validate lifetime/reset/eye identity, moving hands, menus, save reload and recovery. Preserve fallback paths. Ship a default only when evidence supports it.
8. Restore temporary settings, remove simulator markers/command files, close the simulator and verify installed hashes. Do not package test saves, traces, markers, machine paths or diagnostic overrides.

Useful retained tools: `tools/benchmark_cpu_caches.ps1`, optional `vr cpuprofile <tag> <seconds> <stride> <detail>`, heartbeat/logs, existing regression tests. Detailed logging/profiling should stay opt-in.

Kernel ETW capture failed with WPR error `0xc5585011` (profiling privilege unavailable). The alternative native thread sampler perturbed timing: unsampled 93.517/92.306 versus sampled 90.866 accepted frames/s. Stack sample counts are investigative clues, not CPU percentages. Nearest-export symbol names are not guaranteed exact function identities.

### Hidden simulator lessons

The explicit `DVR_usemock.txt` plus `DVR_mockpad.txt=1` combination selects the development harness. It supplies native controller input and avoids global keyboard/mouse injection. HMD-only pose changes do not necessarily turn the engine camera in keep-game-axis mode; use native simulated look input and verify actual camera direction/pitch. Screenshot readbacks can create multi-second interruptions; exclude those windows from performance claims.

Hidden direct launches need child-process Steam context (`SteamAppId`/`SteamGameId` 403640), otherwise this machine entered trial mode and save-loading attempts failed. Use the normal menu → Campaign → Continue route, then visually verify actual gameplay before `vr menuinput 1`. Direct loading from the title screen sometimes stuck at acknowledgement. Latest save SLOT35 and earlier benchmark SLOT31 are different locations; do not mistake one for the other. `exec setviewpos` was unsafe for this scene and placed the player out of the map; do not reuse it. Do not write test settings permanently with engine `writeConfig`.

The four temporary marker/input files are `DVR_usemock.txt`, `DVR_mockpad.txt`, `DVR_MockHmd.txt`, `DVR_MockInput.txt`. Remove them after testing. No global OpenXR registry override or desktop takeover is needed. A simulator result still needs suitable real-headset validation.

## 7. Public release and HZDVR boundaries

Retain AFW, DLSS and Native Stereo fallback and the user's graphics choices. Guard hooks by the supported executable/signatures and preserve dynamic-object validation. Avoid machine-specific CPU affinity/process priority changes, global runtime/SteamVR tweaks, or unbounded retries as public defaults.

HZDVR inspired the approach—small independent mirror, persistent frame scheduling, avoiding redundant work and reusing resources—but is a separate project. Its D3D12-specific work, failed scheduling trials, cutscene changes and crashes are not D2 evidence. D2 already had persistent frame machinery and requires D3D11-specific synchronization. Do not blindly port HZD patches or claim equivalent gains.

Historical `STATUS.md`, `FORK-STATUS.md` and earlier installed notes describe older builds. Prefer this current-state section for what is installed now; use dated reports for their evidence and limitations, not as automatic instructions.

## 8. Backups, source and evidence index

Local workspace root: `<workspace>`.

| Location under workspace | Contents |
|---|---|
| `output/d2-three-pass-backup-20261031-191946/` | Full pre-experiment mod/source/settings/assets/helpers/save backup, git diff/context, hashes, `restore-installed.ps1`. Not base-game data or complete external toolchain. |
| `output/d2-startup-20261030/` | Audio-affinity failure and startup resolution. |
| `output/d2-aa-session-20261030-040331/` | AA dependency/fallback fix notes and prior binaries. |
| `output/d2-perf-20261030/` | SteamVR lock ownership, allocation-free dispatch, shader caching, colour reuse. |
| `output/d2-quest-drop-20261030-045308/` | Real Quest/VD mantle-triggered fallback and bounded recovery. |
| `output/d2-detection-20261031-160157/` | Detection/combat colour records and motion-blur hook notes. |
| `output/d2-cpu-combat-20261031-162109/` | Combat/heavy-hit comfort hooks and sampled HUD diagnostics. |
| `output/d2-cpu-release-20261031-163307/` | Cache trials and reproduced automatic AFW-transition recovery. |
| `output/d2-sim-cpu-20261031-170943/` | Motion-statistics/harness investigation and raw captures. |
| `output/d2-etw-20261031/` | Native sampling, guarded-singleton A/B evidence and backups. |
| `output/d2-contention-20261031/` | Readiness-lock optimization and visual investigation. |
| `output/d2-three-pass/` | Rejected draw/snapshot/preflight comparisons and experimental archive. |
| `output/d2-submission-diagnostic-20261031-200451/` | Removed diagnostic build/source/tests, simulator logs, installation history and removal marker. |

Detailed repository reports: `docs/CPU-CACHE-CANDIDATES-2026-10-31.md`, `SIMULATOR-CPU-2026-10-31.md`, `CPU-THREAD-INVESTIGATION-2026-10-31.md`, `RENDER-CONTENTION-2026-10-31.md`, `CPU-THREE-PASS-2026-10-31.md`, and the now-historical `SUBMISSION-DIAGNOSTIC-2026-10-31.md`.

This reference is also copied into the repository as `docs/PERFORMANCE-REFERENCE.md`. The installed proven DLL remains the exact restored backup; a fresh source rebuild can have a different binary hash without implying another accepted performance change.
