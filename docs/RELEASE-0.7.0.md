# Dishonored 2 VR 0.7.0

Download **DishonoredVR-Setup-0.7.0.exe** below. This is the complete offline setup:
AFW, NVIDIA DLSS, wheel artwork, the mod and PSVR2 helper are included. No manual
DLL copying is required. You must own and install the supported Steam game.

## Setup and updates

- Fresh installations default to AFW + DLSS Quality (supported NVIDIA RTX GPU required). Clear that setup option for Native rendering.
- Existing graphics/renderer settings and saves are preserved.
- The Play shortcut checks for verified stable releases and verifies installed files before launch. Steam's normal shortcut bypasses update checks.
- Verify / Repair restores missing or changed mod files from the local cache; rollback restores the prior installation.
- PSVR2 adaptive triggers additionally require PSVR2 Toolkit; its setup link is provided.
- Existing 0.6.0 users should run this installer once to upgrade the manager to the complete package format.

## Performance and stability

Includes the tested CPU lookup/readiness improvements, shader caching, bounded
GPU-resource reuse, corrected SteamVR worker locking, AFW transient recovery and
AA-change recovery. Combat-effect handling and blur controls are retained.
Graphics quality is not reduced. The temporary submission diagnostic and failed
CPU experiments are not enabled in this release.

The performance reference is bundled and available in docs/PERFORMANCE-REFERENCE.md.
Simulator improvements are not a guarantee of 90 FPS in every scene. The reported
sky smearing/square artifacts remain unresolved; no fix is claimed here.

## Validation and components

Five native regression contracts, installer transaction/repair/update tests,
complete-payload installation tests and real AFW/DLSS GPU smoke checks passed.
The setup is unsigned. Source is GPLv3; bundled third-party runtimes and artwork
retain their separate notices and rights. AFW credits PureDark; DLSS credits
NVIDIA; wheel artwork credits Arkane/Bethesda/ZeniMax. Original mod and community
credits are retained in the repository.
