> HISTORICAL: Removed from the installed mod and active source on 2026-10-31 at the user's request. See PERFORMANCE-REFERENCE.md for current state. The following is the original test report.

# Submission diagnostic build — 2026-10-31

Installed DLL SHA256: `FB253B480C87475F461F819D179466335161AA8C9341EBB56859ECC0B7A4AD93`.

This is a diagnostic build, not a measured FPS improvement. Graphics quality, render scheduling and the accepted readiness optimization (mask 16) are unchanged. Rejected draw-rejection and transform-snapshot experiments have been removed from the active renderer source and preserved in `output/d2-three-pass/experimental-source` and its experimental DLL. `GpuTrace.cpp` matches the pre-experiment backup exactly; it was explicitly recompiled to avoid stale incremental objects.

Relative to that proven source backup, production source changes are limited to `WaitTrace.h` and `Commands.cpp`; CMake adds the diagnostic test. The benchmark retains its camera-pitch movement guard and accepts only the restored renderer's supported masks (0–31).

## What it records

- Existing submission/wait wall timings, plus calling-thread CPU time during selected xrEndFrame calls.
- CPU samples and their corresponding wall samples are recorded atomically into the same snapshot window: `xrEndCallerCpu` and `xrEndSampleWall` counts should match.
- Automatic sampling starts after a window with mean xrEndFrame above 3 ms, or any submission above the 90 Hz budget (11.11 ms). It continues for eight quiet heartbeat windows after the last trigger. It cannot retrospectively CPU-sample the first triggering call.
- Persistent `submission_diag` log lines contain game Present rate, accepted XR submissions per second, runtime/running state, camera position, horizontal direction, camera pitch and existing engine pacing status. These are not headset display FPS.
- Logging is normally every fifth heartbeat, and every heartbeat while sampling or arming a probe. No detailed per-draw profiler is enabled. Existing local log rotation and 64 MB session cap remain in place.

Manual `vr waitcpu 1` forces sampling; `vr waitcpu 0` returns to automatic sampling. Neither command changes the rendering schedule. Manual sampling resets on restart. No commands are needed for the user's next session.

Windows thread CPU accounting is coarse: interpret sufficiently long window aggregates, not individual peaks. At very short call times, tick quantization can even produce a sampled CPU mean larger than the wall mean; that is measurement resolution, not negative waiting. The SteamVR dispatch path measures its calling/waiting thread rather than the dispatch worker. VDXR uses a direct call. Wall-minus-CPU alone does not distinguish runtime blocking from OS scheduling delays or identify the root cause.

## Validation

Release build and all six regression tests passed. Tests cover slow/spike arming, matched wall/CPU pairs, sleeping versus busy work, eight-window retention, disabling after quiet windows and manual sampling. `git diff --check` passed.

Hidden simulator loaded Continue with the user's latest save. Final build was exercised with both controllers tracked, AFW active, mask 16 and horizontal/vertical look inputs. Persistent log showed changed horizontal direction and camera pitch, including pitch -1.5533, with corresponding frame/submission timing. Simulator xrEndFrame remained around 0.02–0.03 ms and does not reproduce the earlier approximately 8 ms real-headset wait. Screenshot capture operations caused long interruptions; those windows are not evidence of ordinary turning performance.

An initial fixed-view probe-off/on comparison was invalidated by player movement. It is retained with an invalid marker and is not used to claim zero overhead or a speedup. Actual Quest/VD overhead and the sustained slowdown still require a headset session.

## Installed state and next test

The simulator is closed and its four marker files removed. Built and installed DLL hashes match. Mod INI and engine configuration match their pre-test backups; all saved-game/profile hashes are unchanged. No pending game commands remain. No global runtime or desktop input settings were changed.

On the next Quest/VD session, use Continue, look around in the troublesome area, hold the slow direction for roughly 15–20 seconds if the drop occurs, then look back and finish normally. `DishonoredVR.log` records the evidence automatically; it is best inspected before several further launches rotate it away. A solid 90 FPS result is not yet established.

Current evidence and incremental backup: `output/d2-submission-diagnostic-20261031-200451/`. The full pre-experiment backup and rollback script remain at `output/d2-three-pass-backup-20261031-191946/`. Its restore script returns the original installed mod after the game is closed; it intentionally does not overwrite saves.
