> This is an unofficial fork of **The Cosmic Tangerine’s [Dishonored 2 VR mod](https://github.com/thecosmictangerine-cloud/dishonored-2-vr)**. Original mod by **The Cosmic Tangerine**; additional modifications and installer maintained by **Indurhra**. Credits to both The Cosmic Tangerine for his work with the current Dishonored 2 build and VR Stereo Hub for their work with Dishonored 1 VR.

> **Indurhra development fork:** Contains the controller weapon wheel, hand calibration,
> controller-operated VR menu, rendering diagnostics/fixes, story Heart repair, and
> left-only PSVR2 adaptive triggers developed from Cosmic Tangerine's mod.
> See [fork status](docs/FORK-STATUS.md) for validation limits and experimental work.
> This is development source, not a claim that all rendering or performance issues are resolved.
# Dishonored 2 VR

A native VR mod for **Dishonored 2** (Arkane Studios, 2016): play the whole game in a PC VR headset with motion
controllers. Free, open source, unofficial.

- **The view follows your head** in 6DOF, and the left stick walks where you look.
- **Your hands are your controllers**: full arms with IK, hands that hold and grip what they carry.
- **Swing the sword to strike** (the drawn blade hits what it touches), with gestures to block and to charge the power
  attack.
- **The pistol and crossbow fire where they point**, along the weapon as you hold it.
- **Powers come out of your hand**; grenades are thrown with your arm's motion; mines go where your hand points.
- **Grab, carry and throw objects** with the right hand; pick up and carry bodies.
- **Menus, loading screens and the power wheel on a big screen in the room**, the HUD as a watch on your left wrist.
- **Cutscenes play around you** (look anywhere; hold A or X to skip); takedowns, climbing and sliding keep the view on
  your head so they stay comfortable.

> Status: **0.5.0, beta** (it replaces 1.0.0, which was numbered too early). Developed and played on a Meta Quest 3 (Link / Air Link). Other headsets and runtimes are expected to
> work but are less tested: see [docs/STATUS.md](docs/STATUS.md) for what is verified, and the known issues.

## Controller controls

See the **[controller guide](docs/CONTROLS.md)** for the full default layout, PSVR2 button equivalents, movement, interaction, quick slots, menus and adaptive triggers.

- **Weapon / power wheel:** hold left grip, move your left hand toward the highlighted item, then release to equip. B cancels. All available inventory entries use one ring, with no selection resistance or paging.
- **VR menu:** hold X for three seconds (**left Square on PSVR2**), navigate with either stick, confirm with A, and close with B.
- **Interact:** right grip. **Fire / use power:** left trigger. **Sword:** right trigger or enabled swing gestures.

These describe the current fork’s default layout; preserved settings and custom bindings may differ.

## Install

1. Download `DishonoredVR-<version>.zip` from the [Releases](../../releases) page.
2. In Steam: right-click Dishonored 2 > Manage > Browse local files (the folder with `Dishonored2.exe`).
3. Copy `dinput8.dll`, `DishonoredVR.dll` and `DishonoredVR.ini` into that folder.
4. Set the game to **windowed** mode, start it from Steam, put the headset on.

The zip's `README.txt` has everything for players: setup, the controls, the settings and troubleshooting. To
uninstall, delete the three files.

### Requirements

| | |
|---|---|
| Game | **Dishonored 2 on Steam**, current build (1.77.9.0). Not GOG, not Microsoft Store / Game Pass, not *Death of the Outsider* (the mod detects them and stays off). |
| OS | Windows 10 or 11, 64-bit. Nothing else to install. |
| Headset | Any PC VR headset with an OpenXR runtime. Tested: Meta Quest 3 over Link / Air Link. Expected to work: other Quests, Virtual Desktop, SteamVR headsets (see [STATUS](docs/STATUS.md)). |
| Display mode | Windowed (not fullscreen or borderless). |

## Settings

`DishonoredVR.ini` (next to the game's exe) is read at startup; every option is explained in it: turning (smooth or
snap), the button layout, sword and throw gestures, the reticle, the wrist watch, render resolution, the menu screen,
cutscene options, cheats (unlock everything and infinite mana are on by default; ammo and health are normal), and a
few advanced switches. Deleting a line or the whole file gives the default back.

## How it works, in one paragraph

A `dinput8.dll` proxy loads `DishonoredVR.dll` into the game. The mod hooks the engine's view matrix so the game's
camera follows the headset, shows the rendered frame in each eye through OpenXR at its true angular size, and turns
the controllers into the game's own keyboard and mouse input (with a servo that keeps the game's aim on your head).
The first-person arms and weapons are re-posed **on the GPU**: each draw's matrices are factored, the weapon is found
by its equipment entity, and the arm skeletons are rewritten with inverse kinematics to reach the controllers. A set of
engine hooks then makes the game agree with what you see: shots, sword hits, power effects and thrown objects leave
from the hand as drawn. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) explains every subsystem.

## For developers and modders

| Document | What is in it |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | How the mod works, subsystem by subsystem, and a reference of every module. |
| [docs/ENGINE.md](docs/ENGINE.md) | Reverse-engineering reference for Dishonored 2 1.77.9.0: functions, structures, field offsets, the player state machine, the interaction sensor, cvars, and the traps and dead ends. Useful for any mod of this game. |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | Building, deploying, the mock OpenXR runtime, automated testing, the `vr` console commands, debugging, packaging. |
| [docs/STATUS.md](docs/STATUS.md) | Every feature, how well it is verified, known issues, open work. |

Highlights for other projects:

- **`mockxr/`** is a mock OpenXR runtime: it lets the mod run and be tested **without a headset**, with scripted head
  and controller motion, emulated runtime misbehaviour (stalls, lost sessions) and captures of what each eye sees.
- **`tools/`** has Python helpers for static analysis of the game's exe (RTTI / vtables, cross references, callers,
  disassembly) and `symbolize.py`, which turns the crash lines in `DishonoredVR.log` into function names.

Build: Visual Studio 2022 (or its Build Tools), CMake 3.25+, vcpkg; then `scripts\build.ps1` and `scripts\deploy.ps1`.
Details in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

Bug reports are welcome in the Issues: attach `DishonoredVR.log` from the game folder and say what you were doing.

## Support

If you enjoy the mod, you can support its development:
[patreon.com/CosmicTangerine](https://www.patreon.com/CosmicTangerine) ·
[ko-fi.com/cosmictangerine](https://ko-fi.com/cosmictangerine)

## License and credits

Made by **The Cosmic Tangerine**. See [LICENSE](LICENSE). Third-party software linked into the DLLs (Microsoft
Detours, {fmt}, Dear ImGui, the OpenXR SDK loader, JsonCpp) is listed with its licences in `THIRD-PARTY-NOTICES.txt`
in every release zip.

Dishonored 2 is a trademark of ZeniMax Media / Bethesda Softworks, developed by Arkane Studios. This project is not
affiliated with or endorsed by them, and contains no game files or game code.
