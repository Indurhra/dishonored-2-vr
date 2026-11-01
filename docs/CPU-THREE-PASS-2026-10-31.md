# Three-part CPU investigation — 2026-10-31

## Decision

No new performance candidate earned a production default. Restored the exact pre-test installed DLL/PDB, mod INI and engine configuration. The existing readiness optimization (mask 16) remains installed. Graphics quality was unchanged. No stable-90 claim is supported by these simulator tests.

The experimental DLL in this evidence folder is **not a public release** and is not installed. The repository retains experimental, runtime-selectable candidates for further research; its default mask is still 16. Even with those bits off, the experimental binary includes refactored snapshot handling and per-view publication, so it is not byte-for-byte equivalent to the restored production baseline. Do not package it as a proven improvement.

## Implemented and tested

1. Early draw rejection (bit 32): reject shaders without current or previous matrix layouts before constant-buffer lookup. Preserve transform/view identity and previous-only temporal correction. This was slower in the bridge comparison (101.803 baseline versus 100.485 accepted simulator frames/s), so it stays off.
2. Per-view hand-transform reuse (bit 64): publish a coherent immutable snapshot and reuse each worker's local copy until publication generation changes. Retain locked fallback on reset/in-flight publication. Initial isolated comparisons showed small gains, but later views did not reproduce them consistently.
3. Submission wait investigation: added conditional calling-thread CPU accounting alongside existing wall timing. Automatically samples after sustained xrEndFrame wall means above 3 ms; manual control is `vr waitcpu 1`. This is diagnostic, not a scheduling optimization. On the SteamVR dispatch path it measures the waiting caller, not worker CPU. Interpret window means because Windows thread accounting is coarse.

Also tested narrower early rejection (bit 128): factor the same matrix before fetching the hand-transform snapshot; unmovable matrices return early. Temporal previous-eye correction still happens first. No different matrix math or reduced rendering quality.

## Fixed street-view comparison

Hidden OpenXR simulator, both controller poses valid and hands active, AFW4, 3286x3555 output, unchanged quality. Five-second settling period, 15-second measurements. No heavy offline analysis/builds during measurements. Camera position, direction and pitch guarded; all below were valid, with zero new AFW refusals. These are simulator throughput and wall delivery intervals, not headset FPS or CPU-only times.

| Mode | Accepted frames/s | Mean interval ms | p95 ms | p99 ms |
|---|---:|---:|---:|---:|
| Baseline A (16) | 117.416 | 8.520 | 9.374 | 10.021 |
| Reuse A (80) | 116.467 | 8.596 | 9.792 | 10.382 |
| Matrix preflight A (144) | 117.106 | 8.555 | 9.494 | 10.069 |
| Combined (208) | 117.876 | 8.461 | 9.847 | 11.462 |
| Baseline B (16) | 118.015 | 8.435 | 9.566 | 10.319 |
| Reuse B (80) | 117.344 | 8.498 | 9.657 | 10.330 |
| Matrix preflight B (144) | 119.020 | 8.443 | 9.565 | 10.332 |

Intervals exclude the first two profiler seconds; heartbeat throughput uses a slightly different sampling window. This explains small differences between reciprocal mean intervals and accepted frames/s. Baseline means averaged 8.478 ms, reuse 8.547 ms, matrix preflight 8.499 ms. No consistent benefit; combined tails were worse.

Skyward comparisons (`pre_*`) were largely tied around 142–145 accepted frames/s. Earlier `repeat_*` reuse gains varied substantially; do not present the largest as a reliable uplift. Trials with `.invalid.txt` markers are excluded. Moved-hand checks showed no newly reported moved-draw/view mismatches, but are not a proof of all concurrent schedules or visual correctness.

## Submission finding and next useful measurement

Simulator xrEndFrame wall mean was 0.026 ms, maximum 0.059 ms over 120 samples; calling-thread CPU was below accounting resolution. The prior real-headset log showed approximately 8.110 ms mean xrEndFrame. The simulator cannot validate removal of that VD delay. Desktop Present already uses sync 0, and xrWaitFrame is already prefetched. No speculative asynchronous xrEndFrame change or D3D protection removal was made.

The next useful investigation is a real VDXR capture during the slowdown, using the new diagnostic to distinguish CPU work from blocked submission. The restored installed build does not contain that new diagnostic: it is preserved in source and the experimental artifact only. A separately reviewed diagnostic build is needed before that capture. The user's squares/sky-smearing report remains unresolved; these tests do not establish a visual fix.

## Verification and restoration

Release candidate built successfully. All six regression tests passed, including snapshot/reset tests and submission sleep-versus-busy CPU accounting. `git diff --check` passed. No desktop takeover was used.

Backup: `../d2-three-pass-backup-20261031-191946/` contains pre-test binaries, settings, source snapshot/patch, assets/helpers and saves. It is not a copy of base-game data or the complete external build toolchain.

Restored DLL SHA256: `43FCCCDFEDFA6B098524921226D2C1E8DC19706862CC48BA8150020A463356F3`.

`restoration.json` verifies DLL/PDB/INI/engine settings match their backups. Every backed-up save/profile file still matched before restoration; no save/profile overwrite was necessary. Simulator closed, four simulator marker files removed, and no command files pending.
