# Installer

Requires Inno Setup 6. The installer includes RobloxShadeHost and offers ReShade with full add-on support, every effect package from ReShade's official list, the presets from `presets/`, and the optional DLSS5 add-on.

After installing Inno Setup, configure and build:

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --target installer
```

Output: `build/installer/RobloxShadeHost-Setup.exe`.

ReShade is downloaded from reshade.me at installation time. Setup displays the license from that version's source tag before running the official installer. A failed ReShade download or installation stops before copying files, so the user can retry or deselect it. Reinstalling preserves existing ReShade settings.

Effect packages come from `EffectPackages.ini` in the `crosire/reshade-shaders` repository, the same list the ReShade installer uses. Each package is extracted into `reshade-shaders/Shaders` and `reshade-shaders/Textures`, skipping the files the list denies. Those folders are added to the search paths in `ReShade.ini`. A failed package download stops installation.

The presets component reads `presets/downloads.ini` from the `main` branch, downloads each listed preset, verifies its SHA-256, and checks that every effect the preset references was installed. Presets go into `presets/`; existing files are not overwritten. To add a preset, commit it to `presets/` and add its filename and hash to `presets/downloads.ini`.

The DLSS5 component reads `downloads.ini` from the `dlss5-assets` release. Both downloads must pass their SHA-256 checks before either is installed. Missing downloads, a missing or invalid manifest, or `enabled=0` skip DLSS5. The finish page reports the skipped component; the setup log records the reason. Cancelling a download stops preparation.

Credits appear before component selection and are installed as `CREDITS.txt`. Removal requests go to **tiago@mouta.me**.

## Maintaining the downloads

The manifest source and asset release notes are in `vendor/dlss5/`. The binaries belong in release assets, not Git. Upload updated `downloads.ini` to the same release when changing download URLs or checksums. The installer reads that release asset, so an application release is not required to update it.

To withdraw DLSS5, remove the two binary assets or upload a manifest with `enabled=0`. Older installers will skip it on their next run. Existing installations are not changed.

## Unattended installation

Host only:

```powershell
.\RobloxShadeHost-Setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /COMPONENTS="host"
```

To install ReShade unattended, first read its license and explicitly pass `/ACCEPTRESHADELICENSE=1`. Select `host,reshade` with `/COMPONENTS`, adding `reshade\presets` for the presets and `reshade\dlss5` for DLSS5. Use `/LOG="setup.log"` to record download failures.

Uninstall removes installed binaries and the shortcut. It retains ReShade settings and files created later by the user.

## Verification

Run `./tests/installer_tests.ps1` after building the host. The tests compile an installer without uninstall registration or shortcuts and install into fresh folders under `build/installer-tests`. They check component selection, license acceptance, effect and preset installation, configuration preservation, and unavailable DLSS5 downloads. Internet access is required for ReShade.

Add `-DownloadDLSS` to also download the published DLSS5 files and verify their hashes against the repository manifest.
