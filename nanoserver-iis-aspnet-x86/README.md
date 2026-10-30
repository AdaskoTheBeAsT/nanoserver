# Nano Server 2025 — IIS + x86 ASP.NET Core 10 Host, Sample API & COM/STA Test Suite

## 1. Build, run, and push the reusable Host image

From this directory:

- Build: `go.ps1` (`docker build -t adaskothebeast/nanoserver:iis-aspnet-x86 .`)
- Run: `run.ps1` (`docker run --rm -it -p 8080:80 adaskothebeast/nanoserver:iis-aspnet-x86`)
- Tag & push: `push.ps1`

Override the runtime version with `--build-arg DOTNET_VERSION=<version>`.

## 2. Build and run the Sample App image (`HealthApi`)

From `HealthApi\`:

- Build: `go.ps1` (`docker build -t adaskothebeast/nanoserver:iis-aspnet-x86-sample .`)
- Run: `run.ps1` (`docker run --rm -it -p 8080:80 adaskothebeast/nanoserver:iis-aspnet-x86-sample`)
- Verify: `curl.exe --fail http://localhost:8080/health` (returns HTTP 200 with `{"status":"Healthy"}`)

## 3. Build and run the COM & STA Interop Test Suite (`Tests`)

From `Tests\`:

- Build: `go.ps1` (`docker build -t adaskothebeast/nanoserver:iis-aspnet-x86-tests .`)
- Run: `run.ps1` (`docker run --rm adaskothebeast/nanoserver:iis-aspnet-x86-tests`)

### Included Test & Native COM Assets (`Tests\`)

- `Tests\src\NativeCOM\` — C++ ATL COM server (`NativeCOM.dll`) source code (`StringConcatenator` coclass, IDL, RGS, vcxproj)
- `Tests\src\NativeLifetimeProbe\` — C++ native lifetime probe DLL source code
- `Tests\native\` — Prebuilt x86 `NativeCOM.dll`, `NativeCOM.manifest`, `Interop.NativeCOM.dll`, `NativeLifetimeProbe.dll`, and VC++ runtime DLLs
- `AdaskoTheBeAsT.Interop.COM` 4.2.0 is consumed as a NuGet package; its library source is not vendored here.
- `Tests\AdaskoTheBeAsT.Interop.COM.Test\` — Managed xUnit v3 test suite executed across all 4 STA shim toggle modes (`Default`, `OS-Level STA Shim Only`, `Library In-Process STA Shim Only`, and `Both Disabled + Runtime Toggle`)

- `Tests\native\NativeShimRegression.exe` — Native OS-shim contracts through normal `user32.dll` imports, run before the managed modes. Shared source: `..\sta-shim\tests\native_regression.cpp`. Rebuild all native assets from the repository root with `sta-shim\build_shim.ps1`. *(Note: `.exe` files are ignored by Git and compiled locally; for unknown heuristic reasons, Bitdefender may flag `NativeShimRegression.exe` as unsafe.)*

See the [native support table](../README.md#native-sta-shim-support-and-limitations). The managed `NanoServerWindowManager` tests retain known deviations of the pinned 4.2.0 package; they do not establish native shim conformance.

The default container command runs `iis_service_monitor.exe`. It starts WAS and W3SVC, reports startup errors, and exits nonzero if either service stops. Replacing `CMD` also replaces this monitoring behavior.
