# Windows installer and update manager

The build and tests produce local artifacts; release publication is a separate step.

The Inno Setup wrapper installs a per-user .NET Framework manager (available on
supported Windows 10/11 systems), an offline payload, Start-menu shortcuts, and a
Windows Apps uninstall entry. It discovers Steam roots from the registry and
custom libraries from `libraryfolders.vdf` / appmanifest 403640. Multiple copies
get a choice; manual browsing is available. Compatibility matches GameInfo.h's
version, executable length and PE timestamp, not just the executable name.

## Player behaviour

- Select PSVR2, another headset, or automatic identification. A read-only check
  looks for the Toolkit component in registered SteamVR driver directories.
  The PSVR2 helper and loader are always included. Fresh Other-headset INIs set
  Psvr2Triggers=0; PSVR2/Auto set 1. The mod still checks headset/runtime identity.
- Existing INIs are never overwritten or merged, including existing trigger
  preferences. Switching the selection during an update does not override them.
- PSVR2 Toolkit itself is a separate dependency, with an official setup link.
  Detection is not a guarantee that its driver is loaded or controllers connected.
- No runtime switching, SteamVR profile edits, power-plan changes or CPU tuning.
- The game and helper must be closed before any file operation.
- Existing managed-path files are backed up by content hash. Existing settings,
  saves and unrelated files are untouched. Modified owned binaries block updates
  and uninstall rather than being silently discarded.
- Rollback restores the previous installation and preserves current settings.
  It pauses auto-updates to avoid immediately reapplying the version just removed.
- Uninstall restores pre-installer binaries, removes owned binaries, preserves
  settings, and leaves `.dvr-install` backups in the game folder intentionally.
  Empty mod directories and the manager's installation.json may remain.
- Use the Play shortcut for automatic updates. Launching straight from Steam
  bypasses update checks. There is no background service or scheduled task.

## Update feed and release contract

The only feed is the public GitHub latest-release endpoint for
`Indurhra/dishonored-2-vr`. No credentials are read or shipped by the updater.
Only stable, newer numeric `vMAJOR.MINOR.PATCH` tags are eligible. Publish both:

1. `DishonoredVR-Setup-MAJOR.MINOR.PATCH.exe`
2. `DishonoredVR-Payload-MAJOR.MINOR.PATCH.zip`

The updater requires GitHub's `sha256:` asset digest and exact size, checks the
repository-specific download URL and HTTPS redirect hosts, validates every file
in the package against its manifest and fixed allowlist, and rechecks the game
before applying. No unverified download executes. Offline, missing releases,
incompatible schema and corrupt packages leave the existing mod intact. The Play
flow offers to launch that existing installation if checking an update fails.

Automatic updates replace mod payload files, not the manager executable itself.
An update requiring a new manager/schema is rejected with a request for a newer
installer. The installer executable is currently unsigned; Windows may show a
reputation warning. HTTPS/GitHub digest validation is not publisher code signing.
An attacker controlling the GitHub repository could publish a malicious update;
protect release publishing access accordingly.

## Build (PowerShell 7)

Install Visual Studio Build Tools with C++, CMake and Inno Setup 6.7 or later.
Build the mod normally with `scripts/build.ps1`. Supply the officially sourced
PSVR2Toolkit `psvr2_toolkit_capi_loader.dll` and retain its bundled MIT license.
For example, from the repository root:

```powershell
pwsh installer/build.ps1 -Version 0.5.1 -ToolkitLoader C:\path\psvr2_toolkit_capi_loader.dll -InnoCompiler 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
```

Build output goes into a new `local/installer-VERSION` directory. The builder
refuses to overwrite a previous build tree. `-SkipBuild` uses existing mod output;
otherwise it builds all three native targets. Licenses are sourced from vcpkg.
The output includes the offline installer, update payload and SHA256SUMS.txt.
No action publishes a release or changes the real game installation.

## Tests and transaction design

Compile Core.cs, Updates.cs and Tests.cs as a separate console test executable,
referencing System.Web.Extensions, System.IO.Compression and
System.IO.Compression.FileSystem. Run it with a new isolated output directory.
The shipped manager has no command-line compatibility bypass or fake-game mode.
The tests inject a compatibility checker and write failure through core APIs.

Before mutation, the engine acquires an exclusive lock and saves a recoverable
journal plus original content blobs. Errors restore that snapshot. A subsequent
operation recovers any pending interrupted transaction before continuing. Writes
use temporary-file replacement; backups are verified before restoration. Paths
are allowlisted and existing reparse points are rejected.

The 2026-10-26 checks include 33 automated assertions: fresh install, upgrade,
rollback, partial-write failure recovery, changed-file protection, uninstall,
settings/save preservation, missing/invalid package handling, archive traversal,
stable-release selection, foreign download URL/hash rejection, offline update,
the automatic-update pipeline with a simulated download, custom Steam-library
parsing and running-game guards.

Actual Inno installers 0.5.1 -> 0.5.2 were also installed into an isolated test
folder containing a local copy of the supported executable. The manager's real
GitHub check returned no newer stable update, UI rollback returned to 0.5.1,
and the actual Windows uninstaller restored the original proxy, removed the mod
and helper, preserved settings/save fixtures and removed its uninstall entry.
The real installed game DLL hash remained unchanged. The 0.5.2 build was a test
fixture, not a published release. Live download of an actual future release
cannot be verified until one is published; no release was published for testing.
