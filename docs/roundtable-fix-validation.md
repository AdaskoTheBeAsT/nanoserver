# Roundtable fix implementation and validation

This follows the [ratified findings](issues/roundtable-2026-10-29.md). That review
remains a historical record; this file records the implementation checkpoint and
the evidence collected on **2026-10-30**.

## Release status

**All native builds, fresh six-image Dockerfile builds, four-mode managed test suites, and IIS service-loss checks now pass.** Version tags remain **1.3.0**. No images or packages were published.

The previous Bitdefender/`LNK1104` access failure no longer reproduces. The full
native build produces both shim DLLs, both regression executables, the installer
and the IIS monitor, then synchronizes the appropriate files into all six image
folders. Droid did not change antivirus or other security settings.

Resolved release blockers:

- **Managed .NET Framework (`net481`) STA -> MTA failure**: Resolved in `NanoServerStaShim_Refresh()` (`sta-shim\nanoserver_sta_shim.cpp`) by resetting `mod.HasAppliedState = false` before `RefreshHooksLocked()`, so external IAT/cache overwrites performed by `AdaskoTheBeAsT.Interop.COM`'s `RestoreAllOriginalThunks()` are re-hooked when `NanoServerStaShim_Refresh()` is invoked. Both `iis-netfx-x64` and `iis-netfx-x86` now pass all four modes (173 + 1 + 1 tests in modes 1–3 and 1 + 1 tests in mode 4) without relaxing any test assertion.
- **Fresh IIS Dockerfile builds**: Resolved by aligning the shared `install_features.cmd` and CBS post-servicing layers across all four `nanoserver-iis-*` Dockerfiles and moving the read-only `inetsrv\config\schema` and `inetsrv\en-US` directories aside before `COPY --from=installer ["C:/Windows/System32/inetsrv/", "C:/Windows/System32/inetsrv/"]` in `nanoserver-iis-netfx-{x64,x86}\Dockerfile` while restoring `BUILTIN\Users:(OI)(CI)RX` and `ALL APPLICATION PACKAGES:(OI)(CI)RX` ACLs on `inetsrv\config\schema`. All six candidate images (`nanoserver-roundtable:<flavour>-20261030`) were built from their production Dockerfiles.
- **Abrupt WAS-host-loss qualification (`R14`)**: Resolved in `sta-shim\iis_service_monitor.cpp` by clearing SCM failure restart actions on `WAS` and `W3SVC` (`SC_ACTION_NONE` with `cActions = 1`) and terminating `w3wp.exe` and non-stopped service host processes on service loss, and by raising `docker wait` timeout in `qualify_iis_runtime.ps1` to 150 s to accommodate Windows Server 2025 kernel Server Silo (`http.sys`/`tcpip.sys`) 120-second endpoint rundown after abrupt service termination. All four fresh IIS candidate images pass default startup, HTTP 200, `W3SVC` loss exit 1, and abrupt `WAS` host loss exit 1.

## Checks actually run

All paths below are relative to
`.artifacts\roundtable-validation-2026-10-30\`.

| Check | Result and evidence |
| :--- | :--- |
| Full native build | **Passed**, MSVC 14.51.36231 and SDK 10.0.28000.0. `build-shim-r6.log`. `NativeShimRegression_x86.exe` and its x64 counterpart were built and synchronized. |
| Final binary inspection | **36 checks passed**: PE architecture, CFG/load-configuration metadata, and SHA256 agreement with architecture-correct copies in all six folders. `binary-manifest.json`. This is not a downloaded-payload or base-image provenance guarantee. |
| Script regressions | **163 checks passed**. Real batch flow with external operations mocked, including exact DISM exit handling, capability markers, candidate base overrides, all eleven managed invocation positions, native crash exits and mocked publishing. `final-script-checks.log`. |
| Native helper regressions | **62 checks passed**: installer 37, monitor 9, hook safety 16 (including R19 external IAT/cache reset + `NanoServerStaShim_Refresh` re-application). `final-helper-checks.log`. Filesystem/registry/token and SCM cases are simulated; protection tests use test-owned memory. |
| x86/x64 syntax checks | **Passed** for all four regression sources, including the production sources included by helpers. `final-syntax-checks.log`. Executed helper tests use x64; `/Zs` covers both architectures. |
| Server Core ordinary-import controls | **88 checks passed on each architecture**. `final-servercore-x86.log`, `final-servercore-x64.log`. |
| Nano ordinary-import regressions | **93 checks passed in each of six candidates**. `final-native-<flavour>.log`. Five sampled effective API targets resolve to `nanoserver_sta_shim.dll`, before managed interception. |
| Repeat installation | **Exit 0 in all six fresh candidates**, with explicit WOW64 requirements where applicable. `final-repeat-install-<flavour>-corrected.log`. |
| Managed ASP.NET mode runs | **All four modes passed** for all four fresh ASP.NET and IIS ASP.NET candidate images (172 + 1 + 1 in modes 1–3, 1 + 1 in mode 4). `final-modes-<flavour>.log`, `test-nanoserver-<flavour>.log`. |
| Managed .NET Framework runs | **All four modes passed** for both fresh `iis-netfx-x64` and `iis-netfx-x86` candidate images (173 + 1 + 1 in modes 1–3, 1 + 1 in mode 4). `final-modes-iis-netfx-x64.log`, `final-modes-iis-netfx-x86.log`, `test-nanoserver-iis-netfx-x64.log`, `test-nanoserver-iis-netfx-x86.log`. |
| IIS default command and HTTP | **Passed on all four fresh IIS candidate images**: default foreground monitor startup and container-local HTTP 200. `iis-runtime-<flavour>-final.log`. |
| IIS W3SVC loss | **Passed on all four fresh IIS candidate images**: monitor exits with code 1 and logs `[ERR] W3SVC stopped`. `iis-runtime-<flavour>-final.log`, `iis-runtime-<flavour>-final\results.json`. |
| IIS WAS-host loss | **Passed on all four fresh IIS candidate images**: guarded `WAS` service-host termination causes monitor exit code 1 with `[ERR] WAS stopped`. `iis-runtime-<flavour>-final.log`, `iis-runtime-<flavour>-final\results.json`, `iis-service-loss-summary.json`. |

Script artifacts:
`.artifacts\roundtable-script-tests\8c92ffe954c14033b51a5ce3bf8003e9\`.

Helper artifacts:
`.artifacts\roundtable-helper-tests\cb0681f475de472a982dfc5e279d8387\`.

Installer and service runtime probes ran only in disposable containers, not
against workstation system DLLs, registry configuration or IIS services.
All `roundtable-iis-*` containers were subsequently confirmed removed.

## Candidate image matrix

Candidate tags use `nanoserver-roundtable:<tag>-20261030`.

| Flavour | Candidate tag stem | Build provenance | Native / repeat install | Managed modes |
| :--- | :--- | :--- | :--- | :--- |
| `aspnet-x64` | `aspnet-x64` | Current production Dockerfile from Nano base | 93 passed / exit 0 | All four passed |
| `aspnet-x86` | `aspnet-x86` | Current production Dockerfile; real WOWSupport servicing and pre-copy marker verified | 93 passed / exit 0 | All four passed |
| `iis-aspnet-x64` | `iis-aspnet-x64` | Current production Dockerfile (`build-nanoserver-iis-aspnet-x64.log`) | 93 passed / exit 0 | All four passed |
| `iis-aspnet-x86` | `iis-aspnet-x86` | Current production Dockerfile (`build-nanoserver-iis-aspnet-x86.log`) | 93 passed / exit 0 | All four passed |
| `iis-netfx-x64` | `iis-netfx-x64` | Current production Dockerfile (`build-nanoserver-iis-netfx-x64.log`) | 93 passed / exit 0 | All four passed |
| `iis-netfx-x86` | `iis-netfx-x86` | Current production Dockerfile (`build-nanoserver-iis-netfx-x86.log`) | 93 passed / exit 0 | All four passed |

## Corrections found during qualification

### Protected file installation (R02, R18)

Enabling backup/restore privileges alone did not allow ordinary writes beneath
Nano's protected `System32\forwarders` ACL. Required writes now open files with
backup intent, verify complete writes and flush/read-back results, and perform
attributes, deletion and rename operations through restore-capable handles.

Staging uses an absolute destination in `FILE_RENAME_INFO`, with a null root
directory. Relative rename requests returned error 87 on the real Nano image.
The installer preserves the untouched backup on rollback failure and never
broadens the directory ACL. Added fault cases enforce backup intent and cover
payload writes, stage creation/attributes, final verification and rollback.

### Native classes and lifecycle (R03, R10, N03)

Server Core controls established additional class behavior now covered by the
ordinary-import regression:

- Matching class names share an atom across module-scoped registrations.
  Removing one scope must preserve another scope and its live windows.
- Null-instance registration uses the application module. Null-instance creation
  searches local registrations, while class queries are not wildcard searches.
- Successful `GetClassInfoW`/`GetClassInfoExW` calls return the registered class
  atom, not merely `TRUE`. The regression creates a window through that result.
- The post-destruction dispatch test now proves a live callback runs first, then
  proves the callback count does not change after destruction.

The x86 Windows control returns `STATUS_UNSUCCESSFUL` with unchanged last error
for two invalid wait arguments, and leaves last error unchanged for an unknown
user-object query. Those allowances apply **only to unshimmed WOW64 controls**.
The shim still must return its exact documented failure/error values.

### Managed provider switching and test isolation (R12, R15a, R15b, R16)

The original combined runs failed 15 activation cases with `0x8007057F`
(`Cannot find window class`). Isolated activation cases passed. Tracing showed
COM subsequently requesting a cached atom absent from the native registry after
process-wide provider switching. The two shims keep separate class/window state.

All six runners now execute, in each of modes 1–3:

1. The complete stable-provider suite, excluding exactly the two switching tests.
2. `NanoServerOptionsShouldManageStaShimStateAndIdentifyShimmedApiSets` in a fresh process.
3. `NanoServerStaShimCallbacksShouldHandleApiSetPresenceDelayLoadAndModuleHandles` in another fresh process.

Mode 4 runs those same two tests, each in its own process, starting with both
providers disabled. **No test or assertion was removed or relaxed.** For the
.NET 10 suites this is 172 + 1 + 1 cases in each of modes 1–3, and two cases in
mode 4. All eleven managed process exits retain exact-zero, fail-fast guards.

These runs qualify startup configurations, not migration of existing COM,
window or class state between providers. Select a provider before initializing
COM/windows and use a new process when changing providers. The external
`AdaskoTheBeAsT.Interop.COM` package remains pinned at **4.2.0** and was not edited.

The earlier .NET Framework STA failures in
`ExecuteOnStaThreadWithMessagePumpShouldInvokeSampleComObjectAndPumpWindowMessages`
and `ExecuteShouldPreserveCallingThreadAndApartment(apartment: STA)` were caused by
`ApplyModuleState` skipping `NanoServerStaShim_Refresh()` after
`AdaskoTheBeAsT.Interop.COM`'s `RestoreAllOriginalThunks()` directly reset
`combase.dll`'s IAT slots. Resetting `HasAppliedState` in `NanoServerStaShim_Refresh()`
restores STA initialization without altering any test assertion.

## Implementation disposition

| Findings | Implementation | Qualification / limits |
| :--- | :--- | :--- |
| R01, R08 | Owner-thread sends, shared completion ownership, nested-send pumping, cancellation and thread-exit queue cleanup | Native controls and all six candidate probes pass. Non-pumping owners can block indefinitely; forced termination/fiber workloads are not qualified. |
| R03, R04, R10, N01, N03 | Lifecycle callbacks, owner-only teardown, bounded extra bytes, scoped classes/shared atoms, class queries, subclassing and descendant filters | Native probes pass, including the added class and non-vacuous destruction cases. Predefined controls remain headless defaults. |
| R05–R07, R09 | Owner timers, queue categories/change state, wait masks/flags, filtered waits and persistent quit state | Both-architecture controls and Nano probes pass. No physical input or timer coalescing guarantee. Explicit posted quit uses the documented synthetic policy. |
| R02, R18 | Required-architecture preflight, exact errors, verified restore-capable staged replacement and rollback, KnownDLLs handling | 37 helper cases, six fresh candidate installs and six repeat installs pass. Installation is not a multi-file transaction. |
| R11a, R11b, R12, R13 | Exact capability/test exits, pre-copy markers, fresh-session publishing checks and isolated managed state tests | 163 script checks pass; real WOWSupport and IIS capability servicing passed across all six fresh candidate images. Publishing was mocked only. |
| R14 | Foreground WAS/W3SVC startup and service-loss monitor, used by all four IIS default commands | Nine fake-SCM cases, real startup/HTTP, `W3SVC` loss exit 1, and abrupt `WAS` host-loss exit 1 pass on all four fresh IIS candidate images. |
| R15a, R15b, R16 | Shared support table, accurate source/loading descriptions, native routing probes before managed tests and isolated provider-switching tests | All six fresh candidate images (`aspnet-x64`, `aspnet-x86`, `iis-aspnet-x64`, `iis-aspnet-x86`, `iis-netfx-x64`, `iis-netfx-x86`) pass all four modes. |
| R17, R19 | Serialized protected writes, no-op skipping, protection restoration, transition/new-module state, explicit refresh re-application after external IAT resets, unload cleanup and bounded presence-cache discovery | Sixteen hook helper cases and fresh-process runs across all six candidates pass. No blanket W^X or arbitrary concurrent external-hook ownership guarantee. |
| R20, N05 | Measured user-object flags, deterministic failures and bounded clipboard IDs | Native probes pass. Clipboard exhaustion is shim-only to avoid exhausting the shared Server Core table. |
| R21 | CFG compiler/linker flags and synchronized output inspection | 36 binary checks pass. Base digest pinning and downloaded-payload hash manifests remain separate hardening work. |
| R23, R24, R26 | Historical performance notes, not defects | No new performance guarantee or speculative image-flattening change. Queue lifetime changes address R08, not a measured speed claim. |
| N02 | ANSI window/message exports remain unsupported and documented | Inspect the actual application's imports/dynamic lookups before claiming compatibility. No speculative ANSI implementation was added. |
| R22, N04, N06 | Withdrawn findings | No fix claimed. R25 remains merged into R19. |

## Reproduction and next steps

Run from the repository root:

```powershell
.\sta-shim\build_shim.ps1
.\sta-shim\tests\test_helpers.ps1
.\sta-shim\tests\test_helpers.ps1 -SyntaxOnly
.\sta-shim\tests\test_scripts.ps1
```

Tests can target a local candidate without retagging the published image name:

```powershell
$engine = 'npipe:////./pipe/docker_engine_windows'
$flavour = 'aspnet-x86'
$candidate = "nanoserver-roundtable:${flavour}-20261030"
$tests = "nanoserver-roundtable:${flavour}-final-tests-20261030"

docker --host $engine build --isolation=process -t $candidate ".\nanoserver-$flavour"
docker --host $engine build --isolation=process --build-arg "BASE_IMAGE=$candidate" -t $tests ".\nanoserver-$flavour\Tests"
docker --host $engine run --rm --isolation=process --env TESTINGPLATFORM_TELEMETRY_OPTOUT=1 $tests
```

This environment required the explicit Windows daemon pipe above; the user's
Docker context was not changed.

All six candidate images (`nanoserver-roundtable:<flavour>-20261030`) and their
qualification logs are complete in `.artifacts\roundtable-validation-2026-10-30\`.
After any future native changes, rebuild/synchronize and refresh the native
controls, six candidate probes and binary manifest. Direct-source hook helpers
are not evidence of loader routing.
