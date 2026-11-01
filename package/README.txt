Dishonored 2 VR {VERSION} — Indurhra fork
Original mod by The Cosmic Tangerine. Additional fork work and installer by Indurhra.
Project: {REPO_URL}

INSTALL
Run DishonoredVR-Setup-{VERSION}.exe with the game closed. Choose the supported Steam Dishonored 2 folder and your headset. The complete offline payload includes the tested AFW/DLSS runtimes, PSVR2 helper, wheel artwork and the mod itself. No manual DLL copying.

New settings use AFW + DLSS Quality and the hand-selected wheel. AFW/DLSS requires compatible NVIDIA RTX hardware; untick the AFW setup option to use Native Stereo. Existing DishonoredVR.ini settings are kept byte for byte, including renderer and graphics quality. The game uses windowed display mode.

PLAY / UPDATE / REPAIR
Use the Play Dishonored 2 VR Start-menu shortcut. The launcher checks for stable GitHub updates if enabled, verifies installed files, and starts the game through Steam. Offline updates fail safely and the launcher can use the verified installed version. Direct Steam launch bypasses launcher checks.
Manage Dishonored 2 VR offers Verify / Repair and Roll back. Repair restores missing or changed managed files from its local cache after backing up current files; settings and saves remain untouched. Updates replace only verified managed payload files. Rollback pauses automatic updates. Uninstall through Windows Apps; original conflicting binaries are restored and settings/backups retained.
Upgrade the 0.6.0 manager by running this Setup once; old managers cannot read the expanded schema-2 payload.

CONTROLS
Left grip: hold the weapon/power wheel, point your hand, release to equip; B cancels.
Hold X for three seconds: VR settings menu (left Square on PSVR2). Sticks navigate, A confirms, B closes.
Right grip: interact. Left trigger: fire/use power. Right trigger or enabled swing gestures: sword.
Existing bindings/settings may differ; see the repository controller guide.

PSVR2
The helper and MIT-licensed Toolkit loader are included. Adaptive triggers still require the separate PSVR2 Toolkit driver from https://github.com/BnuuySolutions/PSVR2Toolkit . Other headsets do not need that driver. No global OpenXR runtime or SteamVR settings are changed.

PERFORMANCE / LIMITS
Accepted CPU readiness and singleton optimizations, shader caching, bounded colour-resource reuse, AA dependency protection and bounded AFW recovery are included. Graphics are not reduced by an update. Stable 90 FPS, all combat blur and the reported sky/square artifacts are not declared fixed. Rejected experiments and the removed submission CPU probe are not enabled.

THIRD-PARTY COMPONENTS
See DEPENDENCIES.txt, AFW-NOTICE.txt, NVIDIA-LICENSE.txt and THIRD-PARTY-NOTICES.txt. Separate binaries/artwork are not relicensed under the mod source GPL. This package requires the supported Steam game and includes no game executable, levels or saves.
