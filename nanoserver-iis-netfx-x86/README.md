# Nano Server 2025 — IIS + x86 .NET Framework 4.8.1 Host, Sample API & COM/STA Test Suite

## 1. Build, run, and push the reusable Host image

From this directory:

- Build: `go.ps1` (`docker build -t adaskothebeast/nanoserver:iis-netfx-x86 .`)
- Run: `run.ps1` (`docker run --rm -it -p 8080:80 adaskothebeast/nanoserver:iis-netfx-x86`)
- Tag & push: `push.ps1`

## 2. Build and run the Sample App image (`HealthApi`)

From `HealthApi\`:

- Build: `go.ps1` (`docker build -t adaskothebeast/nanoserver:iis-netfx-x86-sample .`)
- Run: `run.ps1` (`docker run --rm -it -p 8080:80 adaskothebeast/nanoserver:iis-netfx-x86-sample`)
- Verify: `curl.exe --fail http://localhost:8080/health` (returns HTTP 200 with `{"status":"Healthy"}`)

## 3. Build and run the COM & STA Interop Test Suite (`Tests`)

From `Tests\`:

- Build: `go.ps1` (`docker build -t adaskothebeast/nanoserver:iis-netfx-x86-tests .`)
- Run: `run.ps1` (`docker run --rm adaskothebeast/nanoserver:iis-netfx-x86-tests`)

### Included Test & Native COM Assets (`Tests\`)

- `Tests\src\NativeCOM\` — C++ ATL COM server (`NativeCOM.dll`) source code (`StringConcatenator` coclass, IDL, RGS, vcxproj)
- `Tests\src\NativeLifetimeProbe\` — C++ native lifetime probe DLL source code
- `Tests\native\` — Prebuilt x86 `NativeCOM.dll`, `NativeCOM.manifest`, `Interop.NativeCOM.dll`, `NativeLifetimeProbe.dll`, and VC++ runtime DLLs
- `Tests\src\AdaskoTheBeAsT.Interop.COM\` — `AdaskoTheBeAsT.Interop.COM` library source code (including in-process STA/window shim and SxS manifest fallback)
- `Tests\AdaskoTheBeAsT.Interop.COM.Test\` — Full xUnit v3 test suite (`169` tests) executed across all 4 STA shim toggle modes (`Default`, `OS-Level STA Shim Only`, `Library In-Process STA Shim Only`, and `Both Disabled + Runtime Toggle`)
