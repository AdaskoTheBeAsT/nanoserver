# Changelog

Notable changes to the Nano Server base images, native compatibility shims, sample
applications, and build tooling.

Versions below identify the repository and container revision, not the bundled
.NET runtime version.

## 1.7.0 - 2026-11-08

Prepared locally; not yet published.

### Added

- Real `kernel32!_hread` file reads, including reads larger than 64 KiB.
- Headless `RegisterWindowMessageA/W`, `MsgWaitForMultipleObjects`,
  `CreateRectRgn`, `CreatePalette`, `SelectPalette`, and `GetDIBits` compatibility.
  Regions and palettes use synthetic handles; bitmap reads return blank pixels.
- Current-thread `WH_CBT` creation hooks with ANSI/Unicode callbacks, chaining,
  veto, and attachment before `WM_NCCREATE`.
- Standard and extended modeless dialog template support through
  `CreateDialogIndirectParamA/W`, including child controls, `DWLP_DLGPROC`,
  `WM_INITDIALOG`, message results, and cleanup. Supporting APIs include
  `DefDlgProcA/W`, `GetDlgItem`, `GetDlgCtrlID`, and `MapDialogRect`.
- Real-import x86/x64 native regression coverage for legacy reads, message waits,
  GDI buffer bounds, CBT attachment, and dialog lifecycle and template bounds.

### Fixed

- Normalize unsupported `QueryActCtxW` error `ERROR_INVALID_FUNCTION` to
  `ERROR_CALL_NOT_IMPLEMENTED` so MFC can take its existing fallback. Calls still
  delegate to KernelBase; no activation context is fabricated.

### Changed

- Patch lazy compatibility exports only in existing `forwarders\kernel32.dll`
  files, without a shim import injection, root kernel32 copy, or kernel32
  KnownDLL change.
- Update all six flavour tag scripts, README examples, and script regression
  expectations to container revision `1.7.0` (`-v1.7.0`, `-v1.7`, `-v1`).
- Keep compatibility documentation generic rather than naming calculation vendors.
  Headless layout uses fixed font metrics; menus, modal loops, full control
  behavior, and interactive rendering are not implemented.

## 1.6.0 - 2026-11-02

### Added

- Full 32-bit Embarcadero / Borland Delphi VCL runtime (`System.Classes`,
  `Vcl.Graphics`, `Vcl.Controls`, `Vcl.Forms`, `Winapi.FlatSB`,
  `Winapi.MMSystem`), headless primary monitor, and per-monitor DPI support on
  Windows Nano Server 2025:
  - Added 22 missing static and delay-loaded PE exports required by Delphi VCL
    calculation DLLs to `nanoserver_sta_shim.dll` and `install_sta_shim.exe`:
    - `gdi32.dll` (4 exports): `CopyEnhMetaFileW`,
      `GetEnhMetaFileDescriptionA`, `GetEnhMetaFileDescriptionW`, and
      `GetTextCharacterExtra`.
    - `user32.dll` (3 exports): `LoadCursorFromFileA`, `LoadCursorFromFileW`,
      and `SetGestureConfig`.
    - `ole32.dll` (1 export): `IsEqualGUID`.
    - `comctl32.dll` (12 Flat Scroll Bar exports): `InitializeFlatSB`,
      `UninitializeFlatSB`, `FlatSB_EnableScrollBar`, `FlatSB_ShowScrollBar`,
      `FlatSB_GetScrollRange`, `FlatSB_GetScrollInfo`, `FlatSB_GetScrollPos`,
      `FlatSB_GetScrollProp`, `FlatSB_SetScrollPos`, `FlatSB_SetScrollInfo`,
      `FlatSB_SetScrollRange`, and `FlatSB_SetScrollProp`.
    - `winmm.dll` (2 exports): `sndPlaySoundA` and `sndPlaySoundW` (alongside
      `PlaySoundA`).
  - Implemented 11 headless primary monitor (`HMONITOR = 0x00010001`, monitor
    bounds `1024x768`, work area `1024x746`, device `"\\\\.\\DISPLAY1"`) and
    96-DPI per-monitor/system/window shims (`GetSystemMetricsForDpi`,
    `EnumDisplayMonitors`, `MonitorFromPoint`, `MonitorFromRect`,
    `MonitorFromWindow`, `GetMonitorInfoW`, `GetMonitorInfoA`,
    `GetDpiForMonitor`, `GetDpiForMonitorInternal`, `GetDpiForSystem`, and
    `GetDpiForWindow`).
  - Added `shcore.dll` module discovery in `nanoserver_sta_shim.cpp`
    (`RefreshHooksLocked`, `OnDllNotification`, and
    `NanoServerStaShim_RestoreOriginal`) so `SHCore.dll`'s delay-load imports
    (`ext-ms-win-rtcore-ntuser-dpi-l1-1-0.dll!GetDpiForMonitorInternal` and
    `ext-ms-win-rtcore-ntuser-sysparams-l1-1-0.dll!MonitorFromWindow` /
    `MonitorFromPoint`) resolve to the 96-DPI headless monitor shim when
    Delphi VCL initializes `TScreen` and `TCustomForm`.
  - Added native regression checks
    `Delphi_headless_monitor_enumeration_and_info` and
    `Delphi_vcl_exports_and_dpi_on_shim` in
    `sta-shim/tests/native_regression.cpp`, and installer regression checks
    `R02_verified_PE32_comctl32_exports` and `R02_verified_PE32_winmm_exports`
    in `sta-shim/tests/installer_regression.cpp`.

### Changed

- Extended `install_sta_shim.exe` to patch `winmm.dll` in
  `System32\forwarders` and `SysWOW64\forwarders` alongside `user32.dll`,
  `ole32.dll`, `gdi32.dll`, `comctl32.dll`, and `shell32.dll`.
- Updated all six flavour `push.ps1` scripts, `README.md`, and
  `sta-shim/tests/test_scripts.ps1` to container revision `1.6.0` (`-v1.6.0`,
  `-v1.6`, `-v1`).

## 1.5.0 - 2026-11-01

### Added

- Full 32-bit Visual C++ 10.0 / MFC 10.0 (`mfc100u.dll`, `mfc100.dll`,
  `atl100.dll`, `msvcr100.dll`, `msvcp100.dll`), `gdiplus.dll`, and `msimg32.dll`
  runtime and forwarder support on Windows Nano Server 2025:
  - Provisioned `msvbvm60.dll` (from Microsoft's symbol server), `msvcr100.dll`,
    `msvcp100.dll`, `atl100.dll`, `mfc100.dll`, and `mfc100u.dll` (from the
    official Microsoft VC++ 2010 SP1 KB2565063 `vcredist_x86.exe`
    redistributable), plus `msimg32.dll` and `gdiplus.dll` (from Server Core's
    `SysWOW64` and `WinSxS`) during Stage 1 (`AS installer`) of all five
    WoW64-enabled Dockerfiles (`aspnet-x86`, `iis-aspnet-x64`, `iis-aspnet-x86`,
    `iis-netfx-x64`, and `iis-netfx-x86`), and installed them into
    `C:\Windows\SysWOW64` via `install_sta_shim.exe --require-wow64` instead of
    storing the redistributable DLLs in the Git repository.
  - Added 46 missing static and delay-loaded exports required by `mfc100u.dll`,
    `mfc100.dll`, and `gdiplus.dll` to `nanoserver_sta_shim.dll` and
    `install_sta_shim.exe`:
    - `gdi32.dll` (40 exports): `AbortPath`, `AngleArc`, `ArcTo`, `Chord`,
      `CombineTransform`, `CopyMetaFileW`, `CreateEllipticRgn`, `DrawEscape`,
      `EnumEnhMetaFile`, `ExtFloodFill`, `FlattenPath`, `FrameRgn`,
      `GdiComment`, `GetArcDirection`, `GetBoundsRect`, `GetDCDpiScaleValue`,
      `GetGraphicsMode`, `GetMiterLimit`, `GetPath`, `GetPolyFillMode`,
      `GetStretchBltMode`, `GetSystemPaletteUse`,
      `IsValidEnhMetaRecordOffExt`, `ModifyWorldTransform`, `OffsetClipRgn`,
      `PlayEnhMetaFileRecord`, `PlayMetaFileRecord`, `PolyBezier`, `PolyDraw`,
      `PolyPolygon`, `PolyPolyline`, `PolylineTo`, `PtVisible`, `ResizePalette`,
      `ScaleRgn`, `SetArcDirection`, `SetBitmapBits`, `SetColorAdjustment`,
      `SetMapperFlags`, `SetMiterLimit`, `SetPaletteEntries`, and
      `SetTextJustification`.
    - `user32.dll` (1 export): `MapVirtualKeyExA`.
    - `shell32.dll` (3 exports): `SHGetPathFromIDListW`, `DragQueryFileW`, and
      `SHGetSpecialFolderLocation`.
    - `winspool.drv` & `winmm.dll` (2 delay-loaded exports): `GetJobA` and
      `PlaySoundA`.
  - Added broken PE forwarder detection and in-place redirection
    (`IsBrokenForwarderTarget`) in `install_sta_shim.cpp` so existing
    `shell32.dll` forwarder entries pointing to `windows.storage.dll.*` are
    redirected to `nanoserver_sta_shim.<ExportName>` and verified by
    `VerifyPatchedPe`.
  - Added native regression check `VC10_mfc100u_gdiplus_and_shell32_load` in
    `sta-shim/tests/native_regression.cpp` and installer regression checks
    `R02_verified_PE32_shell32_exports`, `R02_optional_wow64_runtimes_copied`,
    `R02_broken_forwarder_rejected_before_patch`, and
    `R02_broken_forwarder_redirected_to_shim` in
    `sta-shim/tests/installer_regression.cpp`.

### Changed

- Extended `install_sta_shim.exe` to patch `shell32.dll` in `System32` and
  `SysWOW64` and remove `SHELL32` and `gdiplus` from `KnownDLLs`.
- Removed duplicate per-flavour `nanoserver-*/sta-shim/` staging folders and
  32-bit runtime DLLs from Git tracking (`nanoserver-*/sta-shim/` is now
  ignored in `.gitignore` and populated on demand by `sta-shim/build_shim.ps1`
  and each flavour's `go.ps1`).
- Updated all six flavour `push.ps1` scripts and `README.md` tag examples to
  container revision `1.5.0` (`-v1.5.0`, `-v1.5`, `-v1`).

## 1.4.0 - 2026-10-31

### Added

- Full 32-bit Visual Basic 6 (`msvbvm60.dll`) ActiveX/COM runtime and
  registration-free TypeLib support on Windows Nano Server 2025:
  - Bundled `msvbvm60.dll` in the `sta-shim` payload and installed it into
    `C:\Windows\SysWOW64\msvbvm60.dll` via `install_sta_shim.exe` across all
    five WoW64-enabled image flavours (`aspnet-x86`, `iis-aspnet-x64`,
    `iis-aspnet-x86`, `iis-netfx-x64`, and `iis-netfx-x86`).
  - Added 35 missing static PE forwarder exports required by `msvbvm60.dll` at
    module load time (`ntdll!LdrpSnapModule`) to both `install_sta_shim.exe`
    and `nanoserver_sta_shim.dll`:
    - `user32.dll` (16 DDE exports): `DdeAccessData`, `DdeClientTransaction`,
      `DdeCmpStringHandles`, `DdeConnect`, `DdeCreateDataHandle`,
      `DdeCreateStringHandleA`, `DdeDisconnect`, `DdeFreeDataHandle`,
      `DdeFreeStringHandle`, `DdeGetData`, `DdeGetLastError`,
      `DdeInitializeA`, `DdeNameService`, `DdePostAdvise`,
      `DdeSetUserHandle`, and `DdeUninitialize`.
    - `gdi32.dll` (14 legacy GDI exports): `CreateBitmapIndirect`,
      `CreateDiscardableBitmap`, `CreateEllipticRgnIndirect`,
      `CreateHalftonePalette`, `CreatePolygonRgn`, `EnumFontsA`,
      `GetBrushOrgEx`, `GetNearestColor`, `GetTextExtentPointA`, `PtInRegion`,
      `RoundRect`, `SetBrushOrgEx`, `SetWindowOrgEx`, and `UnrealizeObject`.
    - `ole32.dll` (5 OLE1/link exports): `CoIsOle1Class`,
      `OleConvertIStorageToOLESTREAM`, `OleConvertOLESTREAMToIStorage`,
      `OleCreateLink`, and `OleDoAutoConvert`.
  - Implemented full ANSI windowing, class registration, message-queue, and
    clipboard APIs (`RegisterClassA`, `RegisterClassExA`, `UnregisterClassA`,
    `GetClassInfoA`, `GetClassInfoExA`, `GetClassNameA`, `GetClassLongA`,
    `SetClassLongA`, `CreateWindowExA`, `DefWindowProcA`, `CallWindowProcA`,
    `GetWindowLongA`, `SetWindowLongA`, `GetWindowLongPtrA`,
    `SetWindowLongPtrA`, `PeekMessageA`, `GetMessageA`, `DispatchMessageA`,
    `PostMessageA`, `PostThreadMessageA`, `SendMessageA`,
    `RegisterClipboardFormatA`, and `GetClipboardFormatNameA`) backed by the
    user-mode STA window and message-queue engine, including Win32-compatible
    `GetClassInfoExA` / `GetClassInfoExW` handling when callers pass an
    uninitialized `WNDCLASSEX.cbSize` (required by `msvbvm60.dll` when probing
    `"VBBubble"` on secondary STA worker threads).
  - Implemented headless 96-DPI GDI device-context, stock-object, font-metric,
    and system-metric shims (`GetDC`, `GetDCEx`, `GetWindowDC`, `ReleaseDC`,
    `CreateCompatibleDC`, `DeleteDC`, `SaveDC`, `RestoreDC`, `GetDeviceCaps`
    with `LOGPIXELSX=96` / `LOGPIXELSY=96`, `GetStockObject`,
    `GetSystemMetrics`, `GetObjectW`, `GetObjectA`, `GetObjectType`,
    `GetCurrentObject`, `SelectObject`, `DeleteObject`, `CreateSolidBrush`,
    `CreateBrushIndirect`, `CreateFontIndirectW`, `CreateFontIndirectA`,
    `CreateCompatibleBitmap`, `CreateBitmap`, `GetTextMetricsW`,
    `GetTextMetricsA`, `GetTextExtentPoint32W`, `SetMapMode`, `GetMapMode`,
    `SetBkMode`, `GetBkMode`, `SetBkColor`, `GetBkColor`, `SetTextColor`,
    `GetTextColor`, `GetSysColor`, and `GetSysColorBrush`) and hooked
    `gdi32.dll` delay-load tables so `msvbvm60.dll` runtime initialization
    succeeds in headless containers.
  - Implemented registration-free embedded TypeLib redirection
    (`Shim_SxsOleAut32RedirectTypeLibrary`) and hooked `oleaut32.dll`'s
    `ext-ms-win-sxs-oleautomation-l1-1-0` delay-load table so
    `oleaut32!LoadRegTypeLib` resolves `TYPELIB` resources embedded inside
    loaded in-process COM DLLs via `LoadTypeLibEx(..., REGKIND_NONE, ...)`
    without `HKEY_CLASSES_ROOT\TypeLib` registry entries or Windows SxS.
- Added native regression tests (`VB6_ansi_*` and `VB6_headless_*` in
  `sta-shim/tests/native_regression.cpp`) and installer regression checks
  (`R02_verified_PE32_ole32_exports` and `R02_optional_vb6_runtime_copied` in
  `sta-shim/tests/installer_regression.cpp`).
- Documented the architecture of legacy 32-bit Visual Basic 6 ActiveX/COM
  calculation libraries and the 5-stage diagnostic/engineering methodology for
  discovering and filling Nano Server OS gaps in `README.md`.

### Changed

- Updated `sta-shim/build_shim.ps1` to sync `msvbvm60.dll` into all five
  WoW64-enabled flavour folders and link `gdi32.lib` into
  `NativeShimRegression_x86.exe` and `NativeShimRegression_x64.exe`.
- Updated all six flavour `push.ps1` scripts and `README.md` tag examples to
  container revision `1.4.0` (`-v1.4.0`, `-v1.4`, `-v1`).

## 1.3.0 - 2026-10-30

### Added

- Foreground IIS service monitor (`sta-shim/iis_service_monitor.cpp` ->
  `iis_service_monitor.exe`, `R14`) set as `CMD ["C:\\iis_service_monitor.exe"]`
  across all four IIS flavours (`iis-aspnet-x64`, `iis-aspnet-x86`,
  `iis-netfx-x64`, `iis-netfx-x86`):
  - Starts `WAS` and `W3SVC` and polls their service state continuously.
  - Clears SCM failure auto-restart actions so an unexpected service crash is
    not masked inside the container.
  - Stops remaining monitored services, terminates lingering `w3wp.exe` and
    service host processes, and exits nonzero when either `WAS` or `W3SVC` stops
    or loses its host process.
- Native ordinary-import regression suite
  (`sta-shim/tests/native_regression.cpp` -> `NativeShimRegression.exe`) and
  host-side helper/script regression suites
  (`sta-shim/tests/installer_regression.cpp`,
  `sta-shim/tests/monitor_regression.cpp`,
  `sta-shim/tests/hook_regression.cpp`, `sta-shim/tests/test_helpers.ps1`, and
  `sta-shim/tests/test_scripts.ps1`, `R15`–`R16`).
- Comprehensive performance benchmark report (`docs/benchmarks/README.md`)
  comparing Nano Server `v1.3.0` against `v1.2.0` and native Windows Server Core
  (`ltsc2025`), plus full roundtable qualification documentation
  (`docs/roundtable-fix-validation.md`).

### Changed

- Hardened the native STA and Win32 message-queue shim
  (`sta-shim/nanoserver_sta_shim.cpp`):
  - `R01`/`R08`: Cross-thread `SendMessageW` dispatches strictly on the owning
    thread via `std::shared_ptr<PendingSendMessage>` shared completion
    ownership, services incoming sent messages while waiting, and cancels cleanly
    on window destruction or owner-thread exit (removing the `v1.2.0` 50 ms
    wrong-thread fallback).
  - `R03`/`R04`/`R10`/`N01`/`N03`: Full window creation and destruction
    lifecycle notifications (`WM_NCCREATE`, `WM_CREATE`, `WM_DESTROY`,
    `WM_NCDESTROY`), owner-thread affinity enforcement, bounded `cbWndExtra`
    storage (`GetWindowLongW`, `SetWindowLongW`, `GetWindowLongPtrW`,
    `SetWindowLongPtrW`), module-scoped window classes with shared/reusable
    class atoms (`GetClassInfoW`, `GetClassInfoExW`, `GetClassNameW`), and
    `CallWindowProcW` subclass chaining.
  - `R05`–`R09`: Lazy owner-thread timers (`SetTimer`, `KillTimer`) with
    low-priority `WM_TIMER` delivery, typed current/changed queue status bits
    (`GetQueueStatus`), wake-mask and flag matching in
    `MsgWaitForMultipleObjectsEx` (`MWMO_INPUTAVAILABLE`, `MWMO_ALERTABLE`,
    `MWMO_WAITALL`), descendant `HWND` message filtering, persistent `WM_QUIT`
    re-posting, and FLS-backed thread-exit cleanup.
  - `R17`/`R19`: Serialized IAT hook writes, no-op write skipping before
    `VirtualProtect`, per-module `HasAppliedState` caching (128x–154x faster
    `ApiSetQueryApiSetPresence` and 2.22x faster `CoInitializeEx` +
    `CoUninitialize`), explicit `HasAppliedState` invalidation in
    `NanoServerStaShim_Refresh()` after external IAT resets, range-checked
    presence-cache discovery, and module-unload cleanup.
  - `R20`/`N05`: Measured `GetUserObjectInformationW(UOI_FLAGS)` buffer
    reporting and bounded clipboard format table (`RegisterClipboardFormatW`).
  - `R21`: Enabled Control Flow Guard (`/guard:cf`) across all native shim,
    installer, monitor, and regression builds.
- Hardened the native shim installer (`sta-shim/install_sta_shim.cpp`,
  `R02`/`R18`):
  - Added `--require-wow64` (`/require-x86`) preflight enforcement for the five
    WOW64-enabled images.
  - Added backup/restore-intent handles (`FILE_FLAG_BACKUP_SEMANTICS`) for
    Nano Server's protected `System32\forwarders` ACLs.
  - Staged PE patching through `.new` / `.orig` with flush, re-read verification,
    automatic rollback on failure, and post-verification KnownDLLs removal.
- Updated Dockerfiles and PowerShell build/test/push scripts (`R11`–`R16`):
  - Propagated exact `DISM` exit codes (`0` and `3010`), verified
    capability-provided files (`SysWOW64\ntdll.dll`, `inetsrv\appcmd.exe`) before
    runtime copy layers, handled read-only `inetsrv\config\schema` files and
    `RX` ACL grants in `nanoserver-iis-netfx-*`, added `ARG BASE_IMAGE`
    overrides to all `Tests/Dockerfile` files, isolated process-wide
    provider-switching tests in `run_all_modes.cmd`, and initialized
    `LASTEXITCODE` safely in `push.ps1`.
- Excluded all `.exe` binaries from Git tracking via `.gitignore` (compiled
  locally via `sta-shim/build_shim.ps1`) and documented the Bitdefender
  heuristic false-positive warning for `NativeShimRegression.exe`.
- Published container revision `1.3.0` (`-v1.3.0`, `-v1.3`, `-v1`, and floating
  flavour tags) for all six images to Docker Hub (`adaskothebeast/nanoserver`).

### Fixed

- Resolved managed `.NET Framework 4.8.1` (`net481`) Mode 1/2 `STA -> MTA`
  re-entry failure (`0x80004005`) caused by `NanoServerStaShim_Refresh()`
  skipping IAT re-patching after managed `RestoreAllOriginalThunks()` (`R17`/`R19`).
- Resolved abrupt `WAS` host-process termination leaving `w3wp.exe` or the
  container alive during IIS failure qualification (`R14`).
- Resolved fresh IIS Dockerfile build failures caused by read-only attributes
  and restrictive ACLs on `C:\Windows\System32\inetsrv\config\schema` during
  Server Core transplantation.

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
