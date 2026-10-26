# Controller guide — Indurhra fork

This is an unofficial fork of [The Cosmic Tangerine’s Dishonored 2 VR mod](https://github.com/thecosmictangerine-cloud/dishonored-2-vr). The original VR mod and its core controls are by **The Cosmic Tangerine**. This guide covers the current fork, including its hand-selected wheel, controller VR menu and PSVR2 trigger additions.

These are the default controls for release 0.5.1. Preserved settings, the classic layout, or custom runtime bindings can change them. Older development notes may describe superseded wheel controls.

## Button names

The mod uses Quest-style names. On PSVR2 Sense, the corresponding physical buttons are:

| Mod / Quest name | PSVR2 Sense equivalent |
|---|---|
| A (right lower face button) | Cross / ×, right hand |
| B (right upper face button) | Circle / ○, right hand |
| X (left lower face button) | Square / □, left hand |
| Y (left upper face button) | Triangle / △, left hand |
| Left / right grip | L1 / R1 |
| Left / right trigger | L2 / R2 |
| Left / right stick click | L3 / R3 |

Runtime binding overrides take precedence. **“Hold X” means the left-hand Square button on PSVR2, not the right-hand Cross button.**

## Movement and actions

| Action | Default control |
|---|---|
| Walk | Left stick; movement follows head direction |
| Turn | Right stick left / right; smooth or snap according to settings |
| Sprint | Click left stick while moving; click again or stop to cancel |
| Jump | A |
| Crouch / stand | B; while sprinting, crouch performs a slide |
| Zoom | Hold right stick click |
| Interact / pick up | Aim or reach with the right hand and press right grip; hold for interactions requiring a long press |
| Drop a carried object | Press right grip again with the default grab-release setting |
| Throw a carried object | Right trigger; hand movement influences the throw |
| Sword attack | Right trigger, or swing the arm with sword gestures enabled |
| Block | Right grip when it is not being used to interact; gesture blocking is also available |
| Use left-hand weapon / power | Left trigger, aiming with the left hand |
| Health potion | Push right stick up |
| Mana potion | Push right stick down |

Potion pushes must be clearly vertical; return the stick to centre between uses. Grabbing and throwing can be changed by settings. The default right grip handles interaction before blocking when an interactable is selected.

## Weapon and power wheel

1. **Hold left grip** to open the wheel at your left hand’s opening position.
2. Keep holding it and **move your left hand toward the item you want**. Your hand’s position chooses the segment; the selected item is highlighted.
3. **Release left grip** to equip the highlighted item.
4. To cancel, press **B**, or return the hand to the opening centre and release without a selection.

The wheel stays at its opening position, facing the head position captured when opened. Moving the hand across its plane selects; rotating the wrist or turning the head does not steer the selection. The sticks do not select wheel items.

All entries supplied by the current in-game inventory appear in **one ring**. This does not unlock unavailable weapons or powers. There are **no pages and no resistance between selections**; left trigger and X/Y do not change pages. Names and ammunition counts use the live inventory. Tracking loss cancels selection rather than equipping an unintended item.

The dark backing is confined to the wheel, rather than covering the whole view. Original artwork requires local wheel assets; the published installer does not bundle extracted game artwork, so missing icons use text labels and a drawn ring. Existing local artwork is left intact.

## VR settings menu

| Action | Control |
|---|---|
| Open the mod’s VR menu | Hold **X for 3 seconds** — **Square on PSVR2** |
| Navigate | Either stick |
| Confirm / activate | A — Cross on PSVR2 |
| Close the panel | B — Circle on PSVR2 |
| Desktop alternative | Insert |

Gameplay input is suppressed while this panel is open. Release buttons and centre the sticks after closing before resuming play. The VR panel is separate from the game’s journal and pause menu.

## Journal, pause and game menus

- **Menu button:** tap for the journal; hold approximately 0.6 seconds for pause.
- **PSVR2 / controllers without an available menu button:** press both stick clicks together as the menu-button substitute; use the same tap / hold timing.
- In game menus: sticks navigate, **A confirms**, and **B or Menu goes back**.
- Either grip cycles to the next tab; left / right trigger selects the previous / next sub-tab.
- Click either stick while in a game menu to bring its screen back in front of your eyes.
- In skippable cutscenes, hold **A or X** to skip.

## Quick slots

Outside the wheel, with the default layout:

| Button | Tap | Hold approximately 0.5 seconds |
|---|---|---|
| Y | Quick slot 1 | Quick slot 3 |
| X | Quick slot 2 | Quick slot 4 |

X selects its quick slot on release. Holding X for three seconds instead opens the VR menu. These use the game’s assigned quick slots; the custom single-ring wheel does not use X/Y to assign slots. The classic layout changes X/Y and menu-button behavior, so this table does not apply to it.

## PSVR2 adaptive triggers

With compatible PSVR2 Toolkit support and trigger effects enabled:

- **Left trigger only:** pistol has heavier resistance and a crisp break; crossbow has a heavier pull.
- Pistol and crossbow firing requires pulling past the break (approximately 80% trigger travel); release below approximately 70% to reset the firing threshold.
- Powers retain their separate existing effects. The right trigger does not receive these weapon-resistance profiles.

The installer includes the bridge and loader; [PSVR2 Toolkit](https://github.com/BnuuySolutions/PSVR2Toolkit) remains a separate dependency. Existing INI settings are preserved, including disabled trigger effects.

## If a control does not respond

Close the SteamVR dashboard and ensure the game has focus and both controllers are tracked. Check whether you are in a game menu, cutscene, or holding an object, since those contexts intentionally change controls. Existing `DishonoredVR.ini` settings and custom SteamVR bindings may differ from this default guide.
