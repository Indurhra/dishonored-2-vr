# Continuation assessment - 2026-10-26

## Left-hand aiming dot

User requested a smaller dot controlled by the left hand rather than the HMD.
Reticle mode 4 draws one white filled dot with a narrow dark outline; no ring
or beam. Display.ReticleStyle selects leftdot (default) or legacy rays;
Display.Reticle still enables/disables it. Shipped candidate INI enables the
dot. It projects the left-hand ray with each presented image's own camera,
uses the existing engine gun/power/pointer landing point when available, and
otherwise the existing fallback range (8m). It is not a guaranteed surface
hit in that fallback case. It is drawn into the scene images, not HUD plane.
Menus/wheel and game-owned scripted arms hide the dot. Invalid/held left-hand
tracking also hides it (guard added after the first image captures; retest).
Native ark_ui_crosshair_style is temporarily 0 during mode-4 VR gameplay;
toggling off or entering a menu restores its previous numeric value if still
ours, without changing its archived string. No native crosshair setting is
permanently written. Other HUD elements retain their own routing.

Release build passed. Simulator session local/sim-session-20261026-120436:
3072x3264 recommendation, 3286x3555 stereo per eye, scale1, cap240. Steady
8-second sample about194 Presents/s (5.15ms) /97 pairs/s (10.3ms/pair), zero
stage failures, unpaired views, dropped pairs, TAA disagreement or fallback.
Per-eye captures leftdot_neutral / leftdot_left25 / leftdot_right25 show
left yaw25 moves the dot about624 pixels horizontally in each eye; rotating
only the right hand25 restores the neutral dot to within0.4px horizontally.
The neutral dot is about43 bright pixels per eye, with no large ring/beam.
Native crosshair numeric value is0 with archived string2 while dot enabled;
turning dot off restores numeric2. Visual native-HUD isolation and tracking-loss
and pause/resume checks still need finishing. Headset size/comfort untested.

## Frame-cap and hand calibration checkpoint (supersedes earlier uncertainties)

Commits use the user-authorized identity Indurhra <Shakozipred@googlemail.com>.
6891620 adds Display.StereoFrameLimit (default 0, opt-in engine frames/s,
30..300; 240 tested). The native visual-settings limiter reads
com_timeScaler * 30. IDA traced options values through 0x5127B0/0x512000,
the limiter at 0x462E0, and its call site at 0x359240. This is the frame
limiter despite the cvar's misleading name; the traced float references were
limiter/UI/screenshot paths, not a simulation-speed control. Numeric cvar slots
are temporarily changed; the archived string/config is retained. Leaving
synchronized stereo restores the previous numeric value only if still ours.
A game-options edit is preserved. This replaces the earlier unproven-cap finding.

14a5595 adds Display.HandScale (default 1, range .65..1.15), vr handscale,
and portable affine-calibration checks. Empty/casting hand geometry scales
about its tracked palm. The wrist/IK target follows that scale; current and
previous palettes and corrected attachment positions use the same saved
calibration, while joint rotations remain rigid. Learned equipped grips keep
native size. This does NOT establish smaller equipped hands, shorter arms,
or correct subjective hand size; those remain separate calibration work.

Simulator session local/sim-session-20261026-114312 used the isolated mock
runtime, 3072x3264 recommendation, stereo 3286x3555 per eye, render scale 1,
unchanged graphics, and the loaded Dunwall room. Candidate automatic startup
applied engine cap 240 and hand scale .85. The 12-second steady sample ranged
around 186..188 Presents/s (about 5.3ms/frame), approximately 93..94 complete
new stereo pairs/s (10.6..10.8ms/pair), versus stock 120 Presents/s /60 pairs/s.
Do not call this 188 headset FPS. During the sample stagefail/unpaired/dropped,
TAA phase disagreement and stereo fallbacks were zero. Returning to mono
restored 120 FPS (8.33ms/frame), at 3817x3895 mono; returning to stereo reapplied
the higher cap and restored about188 Presents/s. Two unpaired views were counted
across the deliberate mode transition, not during the steady sample. No crash.
Capture operations are excluded from steady measurements. The heartbeat's GPU
ms label is a timestamp interval, not a separate GPU-work benchmark.

Matched hand_native_stereo / hand_small_stereo eye captures were visually
inspected; .85 reduces hand size. Native animation continues between captures,
so these are not a pixel-perfect pose-locked A/B. BodyView's corrected palm
matches its controller target to logged precision (approximately .1mm), although
its diagnostic real-arm reference is in another frame and should not be compared
directly to eye-relative game coordinates. Portable host calibration tests pass,
including a rotated/translated palm, distance scaling, limits and NaN handling:
FPS not measured. Release compilation passed with the existing conversion
warnings. No equipped, moving-head, interaction, death/load or headset comfort
verdict has been established for the hand change. Experimental defaults remain
off/identity; do not describe this checkpoint as all requested work complete.

Normal headset DLL/INI, prior logs, config and saves were restored and verified
by local/update-0.5.0/restore-after-simulator.ps1; zero save files changed.
Restoration evidence is in that session's restoration.json. The official DLL
SHA256 remains 53CE4AF1105E601F78C9480035503EC1D0E4754C2F4F0A4639952B89D7848048.
The runtime override is absent after restoration. Simulator source/IDA outputs,
images and generated builds are ignored local evidence; never commit game code.

Targeted wheel investigation (implementation still pending): the actual UI owner
is arkGameLogicStatePowerwheel, not the small player state. Its own StartBendTime
0x4F7490 acquires a token at state+232; StopBendTime 0x4F7530 releases only that
token. A future live option must isolate this pair, preserve power effects and
ordinary pause, and verify lifecycle/reentrancy before use. Native population
0x4F5360 walks the player's inventory, checks availability via 0x4F5BF0 and
creates a 40-byte slot list at state+4048/count+4056. WheelClosing 0x4F64B0 calls
0x4F5710: reads wheelData.selectedInventoryIndex, finds that itemIndex in the
native list and calls native equip 0x7FB350 with its two declaration pointers.
Negative selection cancels equip. This establishes inventory/equip authority;
it does not establish a safe direct UI highlight setter or stable VR input.
Do not substitute an independent inventory or equip a declaration by guesswork.
Scripted-arm smooth handback and live-wheel interaction remain pending.

## HUD correction candidate

Added experimental Display.StereoHud (default 0), live toggle
vr screencfg stereohud 0|1. In stereo gameplay, native UI draws are captured and
lifted using the existing wheel path and submitted once as a VIEW-space quad
at 2 m, 3.2 m wide. Its complete source extent is retained. Scene eye projections
and resolution remain unchanged. Menu and wheel keep their existing placement;
the mutually exclusive gameplay/wheel owners share panel slot 1. The centre dot
is on the HUD plane, not at scene-target depth. Headset comfort remains untested.

MSVC x64 Release build passed (existing wchar-to-char warnings). Simulator
candidate v0.5.0-1-gbeea82d-dirty: 3072x3264 recommendation, engine3286x3555 per
eye, scale1, about120 Presents/s (8.33ms) /60 new stereo pairs/s (16.7ms).
Compared toggle off/on in the same loaded scene; no steady rate loss observed.
Per-eye compositor capture hud_fixed: one projection plus one shared quad.
Red HUD pixel centroids, reconstructed onto that plane, agree within 0.12mm
horizontally and 0.60mm vertically. Both complete arcs are in view. This replaces
the roughly13-degree mismatch seen on the stock path. Raw scene captures have
UI lifted; compositor captures show UI on its own layer. Wheel open/close and
pause/resume return to the appropriate owner without a stuck lift; game state
returns to mode play/sim1. No crash, phase mismatch or stereo fallback observed.
Capture operations excluded from performance samples. Perceptual headset verdict
and more gameplay HUD families still pending. Candidate is not installed for
normal headset use; simulator restoration retains the official release backup.

## First headset report and authorized simulator investigation

User reports world perspective acceptable after update, low apparent frame rate,
oversized/misaligned hands, and doubled native crosshair/HUD. None is fixed yet.
The first headset log confirms 0.5.0 on VirtualDesktopXR / Quest 3, stereo mode 2,
eye recommendation 3072x3264, actual per-eye engine render 3286x3555, scale 1.0.
No stereo-health fallbacks or TAA eye-phase disagreement in that snapshot.
The last headset heartbeat is a MENU snapshot: its 72 FPS and 13.89 ms cannot
be attributed to gameplay. Prediction's pair-gap field is not measured display
throughput. An initial 30 FPS inference from it was withdrawn.

User authorized simulator launch. Built upstream mockxr using copied OpenXR
headers and MSVC, without modifying the official installed mod DLL. Initial
OpenXR startup failed (-2) with installed implicit layers. A local diagnostic
copy of the mock runtime sets the layers' documented disable environment flags
at runtime negotiation. Loader source loads runtime before API layers; this
isolated run initialized successfully. It changes process environment only.
No global layer, runtime or driver settings were changed. This establishes that
the isolation resolves startup, not which individual layer caused failure.

Evidence: local/sim-session-20261026-110042 (ignored, game-derived). Its state.json,
samples, captures, original headset logs and restoration.json identify the run.
Used mock compositor, mod capture, native HUD capture, BodyView diagnostics,
heartbeat counters and direct image analysis. The disparity helper's fixed
pistol/building crops do not fit this empty-hand room and asymmetric eye FOV;
do not interpret its raw shifts as calibrated stereo evidence.

Simulator: 3072x3264 eye recommendation with Quest FOV [-54,40,44,-55] mirrored
for right eye; stationary hands/head; loaded Dunwall room; all sampled states
report mode play and sim 1. No quality settings reduced.

| Comparison | Engine render | Present rate | Complete new image rate |
| --- | --- | --- | --- |
| Stereo control, repeated | 3286x3555 per eye | about 120/s, 8.33 ms period | about 60 pairs/s, 16.7 ms/pair |
| Mono | 3817x3895 | about 120/s, 8.33 ms period | about 120 mono images/s |
| Stereo, simulator pacing disabled | 3286x3555 per eye | about 120/s, 8.33 ms period | about 60 pairs/s, 16.7 ms/pair |
| Symmetric stereo diagnostic | 3817x3895 per eye | about 120/s, 8.33 ms period | about 60 pairs/s, 16.7 ms/pair |

Rates use heartbeat counters and the mod's rolling Present rate. Polling counters
are quantized by one-second heartbeat updates; short wall-time averages range
118-129/s and should not be quoted as precise throughput. The reported GPU ms is
the interval between GPU timestamp samples, not an isolated GPU work duration.
The ceiling persists without mock pacing, but its engine/driver/external owner
is unproven. Do not claim GPU saturation or a confirmed hardcoded game cap.
No measured steady gameplay result below 40 pairs/s here. A 2.2 s hitch occurred
during diagnostic capture/resize work; exclude those operations from steady
performance samples. No crash or eye-pair fallback observed in sampled gameplay.

Native HUD texture contains one health indicator and centre dot, not two added
mod reticles (Reticle=0). Default eyeproj=1 turns eyes roughly -6.6/+6.6 degrees
while native screen-space HUD is still baked into those images. Mock compositor
captures show health-indicator red centroids at about -50.5/-37.2 degrees yaw,
a roughly 13.3-degree mismatch. Left arc is partly clipped, so this is approximate,
not an exact same-feature correspondence. Opposing rotations predict 13.2 degrees
at the centre. This is strong evidence for HUD projection mismatch. eyeproj=0 is
NOT an acceptable shipped workaround: the health indicator became clipped outside
the headset image and render area increased. Correct HUD routing/projection
separately while retaining efficient scene eye views. A comfortable HUD plane
and scene-depth reticle need different placement policies.

Arm investigation: existing vr armlen clamps segment scale to >=1, so cannot
shrink models. BodyView wristback/wristdown affect its diagnostic reference,
not an actual calibration fix. Palm-target error is near zero in internal
diagnostics; that proves target consistency, not correct real-hand alignment.
Need a bounded hand/arm scale control anchored to a verified palm/grip reference,
with weapon/effect/grab transforms kept consistent; verify both empty and equipped
hands and scripted ownership. No guessed shrink or offset was applied.

Ended simulator game and restored normal runtime override state, full INI,
game configuration and prior headset logs with hash checks. No saved-game files
changed. Official 0.5.0 DLLs remain installed. Captures retained as local evidence.
Simulator output cannot establish headset comfort or confirm perceived hand size.
Next fixes: trace the 120/s ceiling, separate/reproject native HUD, implement
independent calibrated hand scale. Keep separate reviewable changes.

## Authorized official update

User subsequently authorized recommended first-test settings. Enabled explicit
FocusGameWindow=1, CutsceneArms=1 and WriteStatusFile=1. The cutscene-arm option
is the existing broad cinematic policy, not the proposed interaction-specific
fix. Verified existing game config already has r_fullscreen=0,
r_skipCameraMotionBlur=1 and r_swapInterval=0; game config was backed up but not
modified. Retained Stereo3D=1, RenderScale=1, controls, cheats and graphics quality.
DisableMetaASW remains 0 to preserve runtime settings. Backup includes matching
DLLs, INI, old logs and game config at
local/update-0.5.0/settings-backup-20261026-104809. Complete INI and CRLF verified.
No launch; FPS not measured. First test question: does gameplay still appear
magnified with this official synchronized-stereo baseline?

After explicit user installation approval, installed official release v0.5.0.
DLL file version: 0.5.0 (v0.5.0). Archive SHA-256:
80e9ce4b0bda790774a50e22e145ea0f569d073d591a022077022432821a7c44.
Both installed DLL hashes match the publisher's release notes.
Backup: local/update-0.5.0/backup-20261026-104625. Run restore.ps1 in that
folder with the game closed to restore the original two DLLs and matching INI.
Backup manifest records hashes of the original mod files, logs and learned grips.

Intentional INI changes: Stereo3D 0 -> 1 (synchronized stereo), add
DisableMetaASW=0 to prevent the new release changing runtime ASW settings,
and update stereo comments. Other settings retained. Complete installed INI
matches prepared content; CRLF verified. Auxiliary mod files unchanged.
No game launch or runtime test. FPS not measured. No resolution, game display
mode, system runtime or driver settings changed. Next user launch must verify
startup banner and resolved settings before judging perspective and performance.

## Baselines and scope

Source baseline: 92d07ab9e347034fe5e3050aad1a6bc0696c8bd8 (0.5.0).
Working branch: codex/d2-vr-assessment. Reference: Dishonored 1 VR-Main,
b15e4d32a720bd4cfdb5ce4d600f3f8fba8d2cec. Reference checkout is read-only.
HZDVR is outside this work. No game launch, deployment, settings changes or
runtime/driver changes were performed during assessment.

Installed DishonoredVR.dll identifies itself as 0.2.0 (c895b17). The existing
October 18-19 session log has the same banner. This is NOT the source baseline.
The executable has version 1.77.9.0 and size 41134632; the historical log reports
timestamp 67A50CDE and supported=true. Independently recheck the PE timestamp
before using engine analysis against it.

## Perspective and depth

Installed INI and historical resolved settings both have Stereo3D=0, RenderScale=1,
CutsceneArms=0. This establishes mono for the recorded session, not the cause of
the reported magnification. DLL identity and historical session must remain
separate from current source behaviour.

Historical observations, not a new test:

- Native eye FOV: L eye [-54,+40,+44,-55] degrees, R [-40,+54,+44,-55].
- FOV override logged as 108 x 110 degrees; orthoscopic blit became ready.
- Render target logged as 3817 x 3895; window 1280 x 960.
- Startup reported r_fullscreen=1. This conflicts with the documented windowed
  requirement and needs investigation; it does not by itself prove magnification.
- No per-view actual projection or compositor capture is available in the files
  inspected. An override log does not prove the rendered matrix matched it.
- FPS not measured. No new simulator or headset run was performed.

The 1.78x correction in current CameraHook::ProjHook is explicitly under
ScreenMode::FlatRender(): it concerns the flat menu screen, not established
gameplay magnification. Do not present it as the confirmed gameplay cause.

Current source has real eye-specific rendering: ViewMatrixHook composes eye poses;
StereoSync holds simulation for the second engine frame; VRRuntime stages the
left texture; XRSession::SubmitPair draws separate sources into eye swapchains.
Mono and stale-eye fallbacks also exist. Runtime correctness remains unverified.

First implementation should instrument, not guess a wider FOV: correlate view
serial and eye with actual final projection, source tangents, image dimensions,
crop/warp and submitted pose/FOV. Calculate angular mapping around the centre
and edges, test known points and aspect ratios, and identify explicit-projection
or alternate-camera paths that bypass the ordinary override. Compare tracked
translation and eye separation separately from angular magnification.

Difficulty: medium to diagnose with captures; potentially high if engine camera
or screen-space assumptions conflict. Single-pass rendering is deferred until
correct perspective and existing stereo are measured.

## Live selection wheel

KbmInject currently maps summed stick axes to relative mouse movement, scaled by
frame time; comments acknowledge neighbouring-slot selection. ScreenMode already
has a dedicated wheel panel placed ahead of the head when opened. Native wheel
mode suppresses movement keys and the aim servo. Engine notes report player ticks
about every 0.17 seconds while the wheel is open. Normal-speed gameplay has not
been established. Do not remove a global pause flag as a speculative fix.

Trace Powerwheel state update/entry/exit, local time controls, native slot mapping,
inventory availability and equip/confirmation/cancellation calls. Use engine
inventory as the authority; never infer ownership from the whole declaration
table, which includes unowned items. Direct selection requires verified equip
semantics, not just the existing AddItem function.

Proposed UI: a dedicated panel initially placed near the selecting hand and fixed
at its opening pose, unoccluded for readability. Capture wheel draws separately
from ordinary HUD; preserve native inventory/equipment state. Add stable highlight,
dead zone, angular hysteresis, release-to-confirm and centre-to-cancel. Tracking
loss, focus loss, load, death and menus must cancel safely without equipping.
Native time slowdown must be isolated without changing ordinary pause behaviour.

Dishonored 1 hud_layout::wheel_input and HUD routing are useful design references.
Its UE3/D3D9 hooks and offsets cannot be transplanted into Void/D3D11.
Difficulty: medium-high, dependent on native selection and time-control tracing.

## Scripted arm ownership

PlayerState already calls GpuTrace::SetAnimSuspend for animation takeover and,
optionally, cinematics. CineArms defaults false. GPU movement, target-pose queries
and GrabHand contain suspension guards. Simply enabling the broad cinematic
switch does not establish interaction-specific correctness or smooth handback.

Trace animation/interaction ownership and every controller-driven joint/object
correction. Preserve head behaviour, return needed limbs and attachments to the
game, invalidate stale identities on load/death, and blend tracking back on exit.
Test enter, normal exit, skip, interruption, pause/resume, death and load, with
real hands moving throughout. Keep this separate from perspective and wheel work.
Difficulty: medium, dependent on reliable ownership signals and attachment paths.

## Tools, validation and next steps

IDA Professional 9.3 is installed with ida.exe and idat.exe. Use an independent
workspace-local database/output for this game; do not disturb another project's
database or running IDA session. Decompile targeted functions only when source
and existing ENGINE notes cannot establish native behaviour. Keep game-derived
decompilation, databases and captures outside tracked documentation.

Existing instruments: mockxr compositor, capture commands, PipeTrace,
CameraHook counters, StereoTaa diagnostics, tools/disparity.py and static RE tools.
The documented scripts/testing batteries are not published in this checkout;
scripts/run-test.ps1 is only a launching smoke test and is not safe to assume a
complete validation suite. CMake is installed; build dependencies are not yet
verified. No offline tests have been run during this read-only assessment.

Order: instrument perspective; verify mapping; fix the demonstrated cause; verify
eye output; trace and implement live wheel; implement separate arm ownership fix.
Use small independent commits. Preserve mono. Do not install or merge without
explicit testing/integration authorization. Before a test candidate, archive the
installed DLLs, full INI, logs and any files to be changed, record hashes and
prepare rollback of the compatible set. Retain user preferences and learned grips.

Every runtime result must include simulator/headset, resolution, rendering mode,
FPS and frame times, failures and regressions. Say FPS not measured when absent.
Investigate results below 40 FPS without reducing settings just to pass. Headset
perspective, perceived scale, comfort and wheel ergonomics require user judgement.
