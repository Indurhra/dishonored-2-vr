# PSVR2 left adaptive trigger

Installed test feature requested on 2026-10-26. Only the left controller receives
commands, including resets. Pistol and crossbow components use the same weapon
effect as the user's RE4 pistol: start 2, end 3, strength 3. All ammo/upgrade
variants share it. Powers use trigger vibration at position 2, amplitude 1,
60 Hz while the analog trigger is above 0.12. This is motor vibration in the
trigger, separate from the existing ordinary controller rumble. The physical
sensation and alignment with firing need headset testing. Firing input remains
unchanged, as in the inspected RE4 plugin; numeric motor zones are not assumed
to be equivalent to normalized OpenXR input positions.

The game-thread observer reads only the current left equipment component, using
the same verified inventory offsets as BodyHide. Gun/crossbow vtables are known;
power components use exact, allowlisted MSVC RTTI names verified in the installed
1.77.9.0 executable. Unknown objects, the Heart and grenades have no effect.
There is no inventory scanning and no new game detour. Classification is cached
until the component class changes. Equipment observations expire after 250 ms.

The render/input path publishes once on every exit from KbmInject::Tick, with
Off as its default. Only ordinary gameplay, valid left tracking, no left grab,
no possession, no cinematic/wheel, and focused PSVR2/SteamVR permit an effect.
Quest/VDXR and other headsets launch no helper, create no mapping and make no
Toolkit calls. Runtime identity checking still occurs in the existing input
tick. The right controller is never targeted.

`DVR_psvr2/dvr_psvr2_bridge.exe` owns Toolkit calls outside the game. It uses a
250 ms heartbeat lease, foreground PID check and parent-process monitor. Effects
are deduplicated. Lost focus/stale input/parent exit/stop reset only the left
trigger. Invalid hand/profile requests become Off; failed commands stop the
helper after attempting a reset. A fresh publish is required after Toolkit
driver reconnection. The production helper has no test focus bypass.

Set `Psvr2Triggers = 0` under `[Gameplay]` in DishonoredVR.ini and restart to
disable. It defaults to 1. Existing PSVR2 Toolkit installation/SteamVR driver is
used unchanged. The loader DLL is copied from the user's RE4 trigger package.
Toolkit status and desired-profile changes are recorded in DishonoredVR.log;
status 2 is ready, 3 missing loader, 4 unavailable CAPI, 6 init failed,
7 driver inactive, 8 command failed, 9 reset failed. These are software status,
not proof that the user felt the requested resistance.

Build: `cmake --build build --config Release --target DishonoredVR dvr_psvr2_bridge`.
Tests: build `tests` and run CTest. Policy checks cover exact gun/power bytes,
headset filtering, menu/tracking/block gates, RTTI allowlist, expiry and timestamp
wrap. A separately built helper uses an isolated fake Toolkit to test actual
shared-memory heartbeats, deduplication, power switching, right-hand rejection,
expiry, explicit stop, parent exit and command-failure reset. All five host tests
pass. No fake DLL or test helper is included in the installed package. Physical
effects and native runtime classification remain pending the user's headset test.

The wheel and ring backing from commit 83a1812 are retained. Back up the installed
DLL and INI before replacing; keep the prior wheel artwork. Only the mod DLL,
INI option and DVR_psvr2 helper folder change.

## Attribution

Trigger command definitions and loader come from Bnuuy Solutions PSVR2Toolkit
(https://github.com/BnuuySolutions/PSVR2Toolkit), MIT licensed; the license is in
src/Psvr2/PSVR2Toolkit-LICENSE and accompanies the installed helper. Bridge/IPC
structure is adapted from the user's local re4-psvr2-plugin source, with new
protocol magic, left-only commands and Dishonored policy. No RE4 gameplay hooks
or REFramework dependency are included.

## PSVR2 startup follow-up (2026-10-26)

The first headset run identified SteamVR's real system string as
`SteamVR/OpenXR : playstation_vr2`. The previous allowlist did not include this
spelling, so the trigger helper stayed inactive. Added that exact spelling and
its regression check; all five host tests pass. Installed DLL SHA256:
3892986496198D2C868895716AF11EEAB95C8D98F547EF61A2F76BF9C0B743EC.
After restart, the bridge reached status 2/result 0 and the game-thread observer
recognized arkCpntPowerGrapple. Physical trigger feel is still pending testing.

For the reported black screen and persistent OpenXR Toolkit screen, backed up
HKCU SOFTWARE/OpenXR_Toolkit and copied RE4's per-app values to DishonoredVR,
excluding its executable module path. RE4 remained unchanged. The copied profile
includes first_run2=1, eye_tracking=1 and the existing RE4 VRS settings. Values
were checked again after launch. The user confirmed they had closed the first
session; its exit must not be attributed to a spontaneous trigger crash.
SteamVR and the game were relaunched through Steam. The title screen rendered,
and the user explicitly confirmed: "Picture visible; Toolkit gone".
The exact cause of the initial black screen was not isolated by an A/B test.
Copied eye-tracking/VRS values are configuration parity, not proof of active
foveation: startup diagnostics still reported tracker_created=0, so no gaze-based
performance benefit is claimed. Logs and registry backup are local under
local/black-screen-20261026. Current wheel/artwork and all other mods are retained.

## Heavier weapons and late firing (2026-10-26)
Installed a separate build based on 0baafff plus only the trigger changes; pending
Commands/ViewMatrixHook rendering experiments were excluded. Pistol uses weapon
mode 2/3/5; crossbow has distinct profile 3 and uses 2/3/7. Power bytes remain
position 2, amplitude 1, frequency 60. Helper still exclusively targets Left.
PSVR2 pistol/crossbow firing now requires analog pull >=0.80, stays held down to
0.70, and retains existing gameplay/latch/grab gates. Powers, menus, right trigger,
and other headsets retain their existing input thresholds. Physical snap-to-shot
alignment requires user testing; motor zones are not normalized analog positions.
All eight host tests passed including updated profile/threshold checks and the
existing fake-Toolkit IPC integration. Installed files hash-verified:
DLL 018FC6DD55828008D6DAB5C4213091B4FB99D81DF842D7B34656C8FF700592A8
Bridge 3137D4B0ABA539EED031ECCA3054341678BC61E9FFE9323FF5796719F861D6CA
Backup: local/heavy-triggers-backup-20261026-213033. Game left closed for user test.
No CPU power, cooling, rendering settings, INI, or Toolkit installation changes.
