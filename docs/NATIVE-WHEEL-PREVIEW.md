# Native-art wheel review

This revision combines the game's original wheel textures with the existing VR
hold/select/release interaction. It has eight items per page, a highlighted rim,
selected-item illustration, native item name, ammo counts and page indicators.
A 41% opacity dark backing fills the ring interior; the surroundings remain transparent and there is no full-screen veil.
Left grip opens, the thumbstick selects, release confirms, B/centre cancels and
X/Y changes pages. This revision does not add physical hand-hover selection.

`WheelRenderer` is shared by the mod and `wheel_preview`. The preview uses D3D11
WARP and a sample inventory; it is not a game capture or evidence of successful
in-game equipping. Its English labels and counts are fixtures. The mod uses the
native available inventory, localized names and counts instead.

Artwork is chosen by primary/secondary item declaration paths, independent of
localized labels and list position. The native inventory rebuild hook refreshes
the snapshot on the engine thread. Count/name changes retain selection; changed
item identity/order clears selection and requires a neutral thumbstick before
another selection. Release resolves the current native entry and checks its
identity again before calling the native equip controller. Unknown items retain
their native label and equip path even without an artwork mapping.

## Local artwork and preview

Game-derived artwork is not committed. PNGs were decoded read-only from
`lib_wheel.iggy` / `lib_wheel.raw.iggytex` and `quick_access_wheel.iggy` /
`quick_access_wheel.raw.iggytex` in the user's installed game. Iggy type-8 texture
metadata supplies the dimensions, format, ID and offset; BC7/RGBA preserve alpha.
Local source/decoding scripts and manifest are under `local/wheel-assets` and
`local/wheel-renders`. The runtime expects these two PNG subfolders beneath
`DVR_assets/wheel` alongside the mod. Missing textures fall back to item labels
and a drawn rim, with an initialization log entry. Artwork loads once when the
overlay initializes, before the first wheel opening.

Build with `cmake -S src -B build -DDVR_BUILD_WHEEL_PREVIEW=ON`, then
`cmake --build build --config Release --target DishonoredVR wheel_preview`.
Run `build/bin/wheel_preview.exe local/wheel-renders/native-assets local/alyx-wheel-preview/raw`.
The seven GPU captures have premultiplied alpha. The local presentation script
`local/present-alyx-wheel.py` converts them to straight-alpha PNGs, checkerboard
previews, a state sheet and an animated preview. The checkerboard is only a
preview background. Native PNG colours are decoded for the XR sRGB compositor.

## Verification and delivery boundary

Release DLL and standalone renderer build successfully. All three host tests
pass: arm calibration, wheel selection and wheel model. The model tests cover
language-independent artwork, ammo variants, missing art, count-only updates,
reordering, changed identity and empty inventory. Seven rendered states were
generated and the full pistol image and six-state sheet were visually reviewed.

The new rebuild hook, artwork and gamma path have not been tested in the game.
Earlier simulator performance measurements apply to the previous text-ring
revision only. The new package is staged in `local/native-wheel-review-20261026`
for appearance review; it has not been installed or launched. The installed
official DLL SHA256 remains
`53CE4AF1105E601F78C9480035503EC1D0E4754C2F4F0A4639952B89D7848048`.

## Approved backing and installation

After visual approval, the user requested a dark backing confined to the ring
and installation for their headset test. Added a radius-410 local disk with
RGBA (12,14,18,105), behind all artwork. Rebuilt both targets and regenerated
all seven previews; inspected the full pistol preview. Installed the rebuilt
DLL, candidate INI (LiveWheel=1) and 75 original-art PNGs with the game closed.
All installed files match the staged SHA256 hashes. The immediately preceding
DLL/INI are backed up at local/wheel-install-backup-20261026-143352.
Installed DLL SHA256:
FB50D0219A524262BF65AE7E6D383AA956B86F8A5F44ADF282C704DF9CEFD0E9.
In-game verification is now pending the user's headset test.
