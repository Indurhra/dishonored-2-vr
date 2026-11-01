# Dishonored 2 VR: simulator CPU investigation, 31 October 2026

## Installed outcome

Object-motion diagnostic statistics are now opt-in (`vr motionstats 1`), off by default. Normal play avoids their inverse/multiply calculations, viewport query, point projections and shared atomic updates. The rendering correction, previous-frame history and output matrices are unchanged. `vr aaprevreset` still resets counters without enabling collection. Heartbeat identifies whether collection is enabled.

The four existing CPU cache candidates remain disabled (`cpufast 0`). Their combined results did not establish a repeatable gain. A further early shader rejection candidate was built, but its gameplay comparison was blocked by the simulator's loading prompt. That candidate was removed from the source and installed build.

The existing AFW transient recovery fix is preserved. No graphics quality reduction was made. Installed DLL SHA256: `5DEF870EDB09E4DA3309024AF75E0243DC72536B5849149336F24994CC7B1C38`.

## Completed measurements

The user's canal-side save was visually verified against the Continue scene. Mock OpenXR supplied fixed head/controller poses at 2633x3428 per eye, pacing ceiling 240 Hz. The user's renderer 4 / DLSS quality 3 and render scale 1 were retained. Native stereo fallbacks and startup/menu captures were excluded. Each listed capture verified active gameplay, valid controllers, active hand transforms and fresh AFW frames. No build or tests ran during timed captures.

These are accepted application submissions per second, not physical headset display FPS, pure CPU execution time or Virtual Desktop streaming results. NPCs/animations still ran; this is not a deterministic engine replay. Fixed synthetic poses also omit normal keyboard/mouse injection costs.

| Run | Motion statistics | Cache mask | Submitted FPS | Additional AFW refusals |
| --- | --- | --- | --- | --- |
| motion_on_a | On | 0 | 87.349 | 0 |
| motion_off_a | Off | 0 | 88.981 | 0 |
| motion_on_b | On | 0 | 88.813 | 0 |
| motion_off_b | Off | 0 | 87.802 | 0 |
| caches_all_a | Off | 15 | 88.288 | 0 |
| caches_none_c | Off | 0 | 89.589 | 0 |
| caches_all_b | Off | 15 | 89.617 | 0 |

The initial diagnostic-off gain did not repeat. Do not advertise a measured FPS improvement from this change. Removing unused diagnostic work is justified independently of that result. The combined cache experiment overlaps the baseline and does not justify a release default.

## Detailed CPU capture

`sim_final_detail` used mask 0, motion statistics off, stride 128, detailed capture for 20 seconds. Zero records dropped; analysis excludes the first two seconds. Accepted AFW interval: mean 11.229 ms, p95 13.476 ms, p99 14.795 ms, maximum 18.894 ms; 7.12% exceeded 13 ms. Profiling can perturb timing. These wall intervals include waits, and are not a CPU-only measurement. Stable 90 FPS needs frame delivery within approximately 11.11 ms consistently; this capture does not meet that.

Repeated instrumented work remains concentrated in Unmap handling, BeginSixDof, draw interception and Map handling. Sampled exclusive means were approximately 0.711 microseconds/Unmap, 0.679 microseconds/BeginSixDof, 0.868 microseconds/DrawIdxInst, 1.758 microseconds/DrawIndexed and 0.312 microseconds/Map. Tens of thousands of calls occur across sampled worker frames. Do not sum concurrent worker durations into a critical-path frame time. These observations identify investigation targets, not proof of the complete CPU bottleneck; an engine/driver call-stack trace would be needed to establish the remaining critical path.

## Simulator support added

The explicit combination of `DVR_usemock.txt` and `DVR_mockpad.txt` containing `1` enables the development harness. It advertises the synthetic native pad from the first connection probe and disables global keyboard/mouse injection. With desktop injection disabled, `vr menuinput 0` reports menu state; after independently verifying gameplay, `vr menuinput 1` reports play and seeds the room-to-game yaw reference needed by AFW. This is a fixed-pose benchmark aid, not a replacement for production input or automatic gameplay detection. Return to menu mode before transitions. Normal launches without both markers retain their existing behavior.

Hidden direct executable launches need the normal Steam app context (SteamAppId/SteamGameId 403640 in the child environment). Without it, this machine entered trial mode and normal saves were unavailable; attempts to load saves in that context crashed. With Steam context, Continue and explicit slot 35 were both visually verified at the canal scene. Native controller acknowledgement of some loading prompts remained unreliable. The automatic loading-confirmation cvar did not reliably solve the final stuck load, so that session was not benchmarked and was terminated. Do not confuse those harness failures with measured gameplay regressions.

## Validation and cleanup

Release build succeeded and all five existing CTest checks passed: AFW recovery, frame dispatch, DLSS color pool, temporal frame and draw CPU cache contracts. Diff formatting check passed. The final rebuild removes only the unmeasured rejection experiment; the diagnostic/harness code matches the completed gameplay trials.

The game is closed. The original DishonoredVR.ini was restored byte for byte, SHA256 `57A946362A22E508E0197B3B38B2449F31BD8DCE7D3BF0E61129FE017961165E`. Simulator runtime/input/HMD/pad markers were removed; no pending command files remain. Runtime selection used only the game-process override, not the system OpenXR registry. Temporary engine overrides die with the test process; restoration commands were also queued before shutdown, but the final loading loop did not finish processing engine commands. No test configuration is included by the public package's explicit file list.

Raw JSONL captures, metadata, profiler CSV/status/memory files, summaries, initial backups and logs are in this evidence folder. Real-headset visual checks, combat/reload transitions and Virtual Desktop timing remain necessary before claiming any wider performance improvement or stable 90 FPS.
