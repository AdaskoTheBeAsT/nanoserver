# Changelog

Notable changes to the Nano Server base images, native compatibility shims, sample
applications, and build tooling.

Versions below identify the repository and container revision, not the bundled
.NET runtime version.

## 1.2.0 - 2026-10-29

### Added

- Headless compatibility stubs for missing Win32 exports used by legacy Delphi,
  C++, VCL, and COM libraries:
  - `gdi32.dll`: `CopyEnhMetaFileA`, `CreatePenIndirect`,
    `GetTextExtentPoint32A`, and `SetWinMetaFileBits`.
  - `user32.dll`: `CreateIcon` and `GetKeyNameTextA`.
  - `comctl32.dll`: `ImageList_GetIconSize` and `ImageList_SetIconSize`.
- PE export-directory rebuilding in `install_sta_shim.exe`, adding missing
  exports as forwarders to `nanoserver_sta_shim.dll` while preserving existing
  exports and forwarders.

### Changed

- Extended shim installation to patch `gdi32.dll` and `comctl32.dll` alongside
  `user32.dll` and `ole32.dll` in both `System32` and, when present, `SysWOW64`.
- Added `gdi32` to the container's KnownDLLs removal list so the patched DLL can
  be loaded from disk.
- Rebuilt and copied the native shim and installer binaries to all six image
  flavours.
- Updated image push scripts and README examples to container revision `1.2.0`.

The new compatibility stubs provide minimal headless behavior. They do not add
full GDI rendering, graphical controls, or desktop UI support.

## 1.1.0 - 2026-10-28

### Added

- Native x86 and x64 COM STA compatibility shims with user-mode windows and
  message queues, installed in all six base image flavours.
- `NANOSERVER_DISABLE_STA_SHIM` opt-out and native exports for refreshing,
  restoring, and querying the shim at runtime.
- A native shim installer with a guard against running on non-Nano Server hosts,
  plus `sta-shim/build_shim.ps1` to build and distribute its binaries.
- The `nanoserver-aspnet-x64` flavour for 64-bit ASP.NET Core on Kestrel, including
  a sample `HealthApi`.
- Per-flavour containerized COM/STA tests against
  `AdaskoTheBeAsT.Interop.COM` 4.2.0, covering default, OS-only, library-only, and
  disabled-at-startup/runtime-toggle configurations.
- Root `test.ps1` and `set-version.ps1` scripts for running the container tests
  and updating container/runtime version references.
- Documentation covering COM activation fallback, shim configuration, Windows
  internals, and the `AdaskoTheBeAsT.Interop.*` libraries.

### Changed

- Updated the ASP.NET Core image runtime from .NET `10.0.13` to `10.0.14`.
- Replaced host-side build, run, and push batch scripts with PowerShell scripts.
- Updated root `go.ps1` to build the native shim before building all six flavours.
- Excluded test folders from base-image build contexts.

### Fixed

- Completed pending CBS servicing after Features on Demand installation and
  checked `pending.xml` before container shutdown to prevent partially serviced
  layers and subsequent `0xc0370111` startup failures.

## 1.0.0 - 2026-10-26

### Added

- Initial Windows Nano Server 2025 (`ltsc2025`) implementation with five reusable
  base image flavours:
  - `aspnet-x86`: 32-bit ASP.NET Core `10.0.13` on Kestrel.
  - `iis-aspnet-x64` and `iis-aspnet-x86`: ASP.NET Core `10.0.13` on IIS 10 with
    ASP.NET Core Module V2.
  - `iis-netfx-x64` and `iis-netfx-x86`: .NET Framework `4.8.1` on IIS 10.
- WoW64 and IIS Features on Demand installation for the relevant images.
- Installerless ASP.NET Core Module V2 extraction and IIS registration.
- .NET Framework and ASP.NET runtime transplantation from Windows Server Core.
- A separate sample `HealthApi` image for each flavour, exposing `GET /health`.
- Batch scripts for building, running, tagging, and pushing the images.
- Floating flavour tags and full, minor, and major container revision tags under
  `adaskothebeast/nanoserver`.
