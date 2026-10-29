# Scene foveation investigation — 2026-10-28

## Status: integration experiment, not an accepted performance fix
Toolkit registration is restored for RE4. Dishonored's scene VRS prototype reached verified scene geometry but regressed frame time. The installed normal build compiles this experiment out. Useful scene foveation with a demonstrated net gain is NOT complete, and the physical-headset heavy-view problem is NOT fixed by this work.

## Toolkit restoration
The existing manifest is now registered at HKLM/SOFTWARE/Khronos/OpenXR/1/ApiLayers/Implicit with DWORD 0:
C:/Users/Damascus/Downloads/OpenXR-Toolkit-PSVR2-v1.1.1-x64/XR_APILAYER_MBUCCHIA_toolkit.json
The initially attempted HKCU registration was not picked up in the RE4 check; it has been removed after verifying HKLM. Windows elevation was required for HKLM. RE4 profile: bypass=0, vrs=1, eye_tracking=1. Other existing RE4 profile values were preserved. A verification launch reported XR_ERROR_FORM_FACTOR_UNAVAILABLE, so actual restored foveation/eye tracking is not yet headset-validated. Restart RE4 with the headset available. The verification instance was asked to close normally.

Dishonored's profile has bypass=1, and Main.cpp sets the Toolkit disable environment variable for this process only unless DVR_toolkit.txt deliberately requests a diagnostic layer. This prevents restoring Toolkit for RE4 from reintroducing Dishonored's unaccepted layer behavior. The separate PSVR2 controller/haptics toolkit was not changed.

## Prototype
New SceneFoveation.cpp/h uses NVIDIA's D3D11 VRS API at the existing scene draw boundary rather than Toolkit's late OpenXR frame/target heuristics. It verifies the draw's PerViewCB against a recorded eye/frame, checks matching colour/depth dimensions and actual viewport, projects the same frame's head-forward direction into the render-eye frustum, and builds a bounded per-eye viewport mask. Central ellipse uses full shading, periphery 2x2. This is FIXED head-centred foveation, not gaze tracking. It does not reduce resolution/depth, change DLSS quality, or add an eye render.

Only known large depth-tested geometry draws qualify. Fullscreen/compute work is not indiscriminately foveated. UI mirrors, unknown draws, compute and command-list completion disable the state. Batching state across consecutive compatible draws avoids repeated NVAPI calls. This is still experimental: lifecycle/device changes, unsupported state paths, mask cache capacity, indirect draws, visual stereo stability and headset validation remain incomplete.

Build requires explicit DVR_EXPERIMENTAL_SCENE_VRS=ON and DVR_NVAPI_ROOT. Default is OFF, with inactive stubs. Diagnostic command: vr scenevrs 0 (off), 1 (classification/control), 2 (VRS). Do not enable for normal play based on these results.

NVIDIA SDK inspected: local/foveation/nvapi, commit 70d337db9186e968eab622f7e786de7e437faf3d. Toolkit matching source and the DLSS/AFW reference documents were cross-checked. No external game rendering implementation was copied.

## Results and limitations
Stationary simulator, loaded Karnaca save, 2820x2876 output, Quality input 1880x1917, symmetric 50-degree half-FOV. These are NOT the user's 79 FPS physical-headset heavy-view conditions. The PSVR2 log used Quality 1755x2285, about 11% more scene pixels, different view coverage, and SteamVR submission cost. Camera direction/visible scene also differs. Do not extrapolate simulator FPS to that view.

Every completed fov*.jsonl comparison was audited: all 20 samples per run reported gameplay, not a pause/menu screen. Each measured AFW interval had one fresh render = one reconstruction = one accepted stereo pair = one temporal scene dispatch, with zero refusals/held ticks.

| Experiment | FPS | ms / stereo pair |
|---|---:|---:|
| Depth-write-only VRS | 88.717 | 11.272 |
| Matching classifier without VRS | 91.231 | 10.961 |
| Fully off, same session | 99.689 | 10.031 |
| Expanded depth-tested VRS | 77.919 | 12.834 |
| Expanded classifier without VRS | 88.947 | 11.243 |
| Batched expanded VRS | 90.790 | 11.014 |
| Fully off, batched-build session | 97.887 | 10.216 |

Native stereo checks with the depth-write prototype: control 56.858 FPS / 17.588 ms; VRS 52.312 FPS / 19.116 ms. No net throughput benefit.

GPU pipeline statistics in the expanded unbatched comparison: mean PS invocations per interval approximately 138,076,811 control versus 133,032,242 VRS (3.65% lower). Earlier depth-write-only coverage reduced this about 2.3%. Counts cover the interval, not exclusively the scene, and do not measure shader duration. They confirm less pixel-shader work but NOT lower total GPU time or improved FPS. Geometry, compute, shadows, engine CPU and AFW costs remain.

PresentMon fov4_gpu_on ended after entering a pause screen and overlapped a separate build; it is rejected as a controlled comparison. The subsequent repeat failed the gameplay precondition and is also excluded. No valid paired total GPU-busy improvement is claimed. The later paused screenshot does not invalidate the separately audited completed FPS runs.

## Crash and validation
First prototype crashed in an NVAPI state-reset path during a real deferred draw. A minimal deferred enable/disable resource smoke passed and did not reproduce it. Serializing state changes and using explicit per-viewport disable avoided recurrence in subsequent runs, but those two changes do not isolate the original root cause. No claim of a vendor driver defect is made. Crash evidence: local/foveation/driver-crash.log and matching PDB.

Subsequent live native/AFW and DLSS mode changes remained operational during the completed samples. DLSS counters showed successful per-eye evaluations without fallback. This does not resolve the separately captured physical-headset missing temporal-input/tone-viewport failure.

Normal Release build, NGX/D3D11 live smoke (both eyes, all SR modes, transient recovery, invalid input rejection and state restoration), temporal-frame-contract ctest and changed-file whitespace checks passed.

## Foveated DLSS assessment
Inspected ClarkCheekyKent/CheekyFoveatedDLSS at commit 5d1894b91446a5dac13c7ee85118d23bcceab92c: README, UEVR compatibility notes, D3D11 backend and NGX interception code. It supports D3D11 and concentrates DLSS processing into a region with peripheral composition. That targets DLSS reconstruction cost, separately from scene VRS. Its documented AFW integration is UEVR/DX12, not this custom D3D11/AFW path. No Cheeky DLL was installed and no code was copied into the mod. Compatibility and gains here remain untested; advertised percentages are not transferable to a CPU-limited view.

## Deployment and remaining work
Installed normal DLL SHA256 C9F861E8895BC3C5B4B61783DFBD6538FA3A824DF6B09D8E4442F3939F7FBE0D, experiment compiled out. Matching PDB installed. Original INI restored byte-for-byte, SHA256 F9FB4425C339F592B7366BA26BE2AD00F2CABD98EA313601B83BEDC4D53E7FC4. Temporary mock/runtime/input files removed from game and retained under local/foveation. Baseline DLL/PDB/log/INI in local/foveation/before. Batched experimental DLL/PDB retained separately under local/foveation/experimental-binary, hash C2A323FE6AA698EBFAA35AA966D6D4519C42803135966AF35A7F8725883DDBCA. No commit/push.

Next engineering requirement is cheap state-change/pass-level scene classification, not repeating resource/state queries on thousands of draws. VRS cannot be accepted until that overhead is removed, GPU cost is measured without pauses, and the same heavy view is tested on the physical headset. Real gaze integration and AFW fresh/held stereo coverage also need validation. This report intentionally records an unsuccessful optimization rather than presenting nominal activation as a performance success.
