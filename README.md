# 🚀 Windows Nano Server 2025 — IIS 10, ASP.NET Core 10 & .NET Framework 4.8.1 (`x86` & `x64`)

**Run the "impossible" .NET workloads on Windows Nano Server 2025 (`ltsc2025`).**

This repository provides community-maintained, reusable **Nano Server 2025 base images** (and sample `HealthApi` apps) published to a single unified Docker Hub repository (**`adaskothebeast/nanoserver`**) for the exact .NET scenarios where Microsoft's official images normally force you onto massive **5–8 GB Windows Server Core** containers:

- ⚡ **32-bit (`x86`) & 64-bit (`x64`) ASP.NET Core 10** — Self-hosted **Kestrel** (with built-in COM STA & Win32 message-queue shim) & **IIS 10 (`OutOfProcess` / `InProcess`)**
- 🌐 **64-bit (`x64`) ASP.NET Core 10 on IIS 10** — **`InProcess`** & **`OutOfProcess`** via ANCM V2
- 🏛️ **32-bit (`x86`) & 64-bit (`x64`) Full .NET Framework 4.8.1 (`net481`) on IIS 10** — Full `System.Web` / ASP.NET Web API / MVC / WebForms

---

## 🏆 The Backstory: Asking Microsoft for 32-bit Nano Server ([`microsoft/Windows-Containers#118`](https://github.com/microsoft/Windows-Containers/issues/118))

Back in 2022, when Microsoft's Windows Containers team asked in [**`microsoft/Windows-Containers#118` (*Add 32-bit x86 Support for Nanoserver*)**](https://github.com/microsoft/Windows-Containers/issues/118) what real-world workloads needed 32-bit (`x86`) support on Nano Server, I ([**@AdaskoTheBeAsT**](https://github.com/AdaskoTheBeAsT)) made the case for the .NET ecosystem:

> *"In industries like HVAC, component manufacturers for water heaters, coolers, fans, etc. provide their calculation libraries as `x86`-only DLLs — plain native DLLs and COM libraries written in VB, C++, Delphi, or even PowerBasic. Putting a modern .NET Web API / worker facade on top of them inside slim Windows containers scaled across a message bus or load balancer is the natural modernization path — but being forced onto a ~5 GB Windows Server Core image is way too heavy for a simple worker or API container."*

Microsoft listened and introduced **Nano Server Features on Demand** (`Microsoft.NanoServer.Datacenter.WOWSupport` and `Microsoft.NanoServer.IIS`) in **Windows Server 2025 (`ltsc2025`)** ([announcement blog](https://techcommunity.microsoft.com/blog/containers/discover-the-new-era-of-windows-server-2025-nano-server-containers/4413060)).

However, turning those raw OS capabilities into **ready-to-use .NET & IIS containers** still required solving offline CBS/DISM package dependencies, installerless ANCM V2 registration, and `.NET Framework 4.8.1` (`x86` & `x64`) CLR transplantation. **These images target the headless scenarios below. Validate the exact native API surface your application uses before deployment.**

---

## 💡 Why .NET Developers Will Love These Containers (Value Proposition)

If you have ever tried to containerize a real-world enterprise .NET application on Windows, you have probably hit the **"Server Core Wall"**:

> *"We have one 32-bit native DLL"* ➡️ **Forced onto 5+ GB Windows Server Core.**  
> *"We need IIS `web.config`, virtual directories, or ANCM"* ➡️ **Forced onto 5+ GB Windows Server Core.**  
> *"Our app is still on .NET Framework 4.8.1 (`System.Web`)"* ➡️ **Forced onto 7+ GB Windows Server Core.**

### ❤️ What You Get Instead

- 🪶 **50%–70% Smaller Image Footprint:**
  - `mcr.microsoft.com/windows/servercore:ltsc2025` starts at **4.85 GB** *before* adding IIS, .NET Framework 4.8.1, or ASP.NET Core (which pushes images to **5.5–8 GB**).
  - These Nano Server 2025 images weigh in at **1.41 GB – 2.48 GB** *including* WoW64, IIS 10, and the .NET runtime — saving gigabytes on every CI/CD pull and AKS/ECS node.
- ⚡ **Dramatically Faster CI/CD Builds & Cold Starts:**
  - Because the heavy OS feature installation (`IIS`, `WOWSupport`, `ANCM V2`, `.NET 10` / `.NET 4.8.1`) is baked into a **pre-built Base Host Image**, your application `Dockerfile` only copies your compiled `bin/` output. App container builds finish in **seconds**, not minutes.
- 🔌 **32-bit (`x86`) Native Interop Without the Bloated OS:**
  - Need to P/Invoke a legacy 32-bit C/C++/Delphi/PowerBasic DLL, hardware SDK, calculation engine, or x86 driver from ASP.NET Core 10 or .NET 4.8.1? The **WoW64 subsystem (`SysWOW64`)** is pre-installed and ready.
- 🌐 **Real IIS 10 + ANCM V2 on Nano Server:**
  - Keep your existing `web.config`, IIS application pools, `InProcess` / `OutOfProcess` ANCM V2 hosting, and classic `System.Web` pipeline (`Integrated` mode) running inside a minimal Nano Server container.
- 🔒 **Smaller Attack Surface & Less Memory Overhead:**
  - Nano Server strips out GUI subsystems, MSI installers, and unnecessary background services, giving your .NET APIs lower idle RAM usage and a hardened production footprint.

---

## 🧩 What Technical Roadblocks Does This Solve?

Out of the box, `mcr.microsoft.com/windows/nanoserver:ltsc2025` **cannot** run any of the workloads above because:

| Roadblock on Standard Nano Server 2025 | How This Repository Solves It |
| :--- | :--- |
| ❌ **No 32-bit (`x86` / WoW64) subsystem** (`SysWOW64` is missing, so 32-bit processes immediately fail to launch) | Installs `Microsoft.NanoServer.Datacenter.WOWSupport` using online `DISM /Add-Capability`, then completes pending CBS servicing and checks capability-provided files. |
| ❌ **No IIS 10 included** | Installs `Microsoft.NanoServer.IIS` using online `DISM /Add-Capability`, configures IIS, and monitors WAS and W3SVC in the foreground. |
| ❌ **No `msiexec` or `netsh` to install ASP.NET Core Hosting Bundle (ANCM V2)** | ✅ Extracts `aspnetcoremodule_x64_en_v2.msi` in a builder stage and uses a standalone C# tool (`dotnet RegisterAncm.dll`) to register `aspnetcorev2.dll` and inject the `system.webServer/aspNetCore` schema into `applicationHost.config`. |
| ❌ **No Full .NET Framework 4.8.1 (`net481`) or `System.Web`** | ✅ Transplants `C:\Windows\Microsoft.NET` (`Framework` + `Framework64`), native CLR CRT forwarders (`mscoree.dll`, `ucrtbase_clr0400.dll`, `vcruntime140_clr0400.dll`, `vcruntime140_1_clr0400.dll`), 32-bit/64-bit `inetsrv`, and NDP/ASP.NET registry hives from Server Core into Nano Server. |
| ❌ **No Single-Threaded Apartment (`STA` / `COINIT_APARTMENTTHREADED`) or Win32 message queue** (desktop window APIs required by COM are omitted) | Installs a native user-mode STA & Win32 message-queue shim (`nanoserver_sta_shim.dll`, 32-bit & 64-bit) into `System32` and `SysWOW64` via `user32.dll` / `ole32.dll` forwarders for scoped headless COM and message pumping (toggleable via `NANOSERVER_DISABLE_STA_SHIM=1`). Qualified across all 6 images; see [validation status](docs/roundtable-fix-validation.md) and [benchmarks](docs/benchmarks/README.md). |

---

## 📦 All 6 Flavours at a Glance

*(Note: While Microsoft publishes `mcr.microsoft.com/dotnet/aspnet:10.0-nanoserver-ltsc2025` for 64-bit Kestrel, it lacks COM Single-Threaded Apartment (`STA`) and Win32 message-queue support. `nanoserver-aspnet-x64` provides an ultra-slim **697 MB** 64-bit Kestrel image with the native STA & message-queue shim pre-installed.)*

| Folder | Docker Hub Tag (`adaskothebeast/nanoserver`) | Runtime | Web Server & Hosting | Bitness | Image Size | Port |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 📁 [`nanoserver-aspnet-x86`](./nanoserver-aspnet-x86) | `:aspnet-x86` | **ASP.NET Core 10.0** (`win-x86`) | **Kestrel** (Self-hosted) | 32-bit (`x86`) | **1.47 GB** | `8080` |
| 📁 [`nanoserver-aspnet-x64`](./nanoserver-aspnet-x64) | `:aspnet-x64` | **ASP.NET Core 10.0** (`win-x64`) | **Kestrel** (Self-hosted) | 64-bit (`x64`) | **697 MB** | `8080` |
| 📁 [`nanoserver-iis-aspnet-x86`](./nanoserver-iis-aspnet-x86) | `:iis-aspnet-x86` | **ASP.NET Core 10.0** (`win-x86`) | **IIS 10** + ANCM V2 (`OutOfProcess`) | 64-bit IIS ➡️ 32-bit `dotnet.exe` | **2.58 GB** | `80` |
| 📁 [`nanoserver-iis-aspnet-x64`](./nanoserver-iis-aspnet-x64) | `:iis-aspnet-x64` | **ASP.NET Core 10.0** (`win-x64`) | **IIS 10** + ANCM V2 (`InProcess` / `OutOfProcess`) | 64-bit (`x64`) | **2.39 GB** | `80` |
| 📁 [`nanoserver-iis-netfx-x86`](./nanoserver-iis-netfx-x86) | `:iis-netfx-x86` | **.NET Framework 4.8.1** (`net481`) | **IIS 10** (`System.Web` Integrated) | 32-bit (`x86` `w3wp.exe`) | **2.04 GB** | `80` |
| 📁 [`nanoserver-iis-netfx-x64`](./nanoserver-iis-netfx-x64) | `:iis-netfx-x64` | **.NET Framework 4.8.1** (`net481`) | **IIS 10** (`System.Web` Integrated) | 64-bit (`x64` `w3wp.exe`) | **2.03 GB** | `80` |

---

## 🏗️ Three-Part Architecture: Base Host, Sample App & Self-Contained COM/STA Test Suite

Every folder is cleanly separated into three parts:

1. 🧱 **Part 1 — Reusable Base Host Container (`<folder>\Dockerfile`)**
   - Built once and pushed to Docker Hub (`adaskothebeast/nanoserver:<flavour>`).
   - Uses `.dockerignore` to ignore `HealthApi\` and `Tests\` so application or test changes never trigger a base image rebuild.
2. 🚀 **Part 2 — Sample `HealthApi` Container (`<folder>\HealthApi\Dockerfile`)**
   - Compiles `HealthApi.csproj` using `mcr.microsoft.com/dotnet/sdk:10.0-windowsservercore-ltsc2025` as a multi-stage build step.
   - Layers the published output onto `FROM adaskothebeast/nanoserver:<flavour>`.
   - Serves `GET /health` ➡️ `200 OK` `{"status":"Healthy"}`.
3. 🧪 **Part 3 — Self-Contained COM & STA Test Suite (`<folder>\Tests\Dockerfile`)**
   - Contains C++ ATL COM server source (`Tests\src\NativeCOM\`), native lifetime-probe source (`Tests\src\NativeLifetimeProbe\`), prebuilt fixtures (`Tests\native\`), and the managed suite (`Tests\AdaskoTheBeAsT.Interop.COM.Test\`). The managed library is the `AdaskoTheBeAsT.Interop.COM` 4.2.0 NuGet package, not source in `Tests\src`.
   - Runs `NativeShimRegression.exe --require-shim` in a fresh process before the managed modes. Its shared source is `sta-shim\tests\native_regression.cpp`; normal `user32.dll` imports avoid managed interception and report final call targets.
   - Builds and executes all unit tests inside `FROM adaskothebeast/nanoserver:<flavour>` across all 4 STA shim toggle modes (`Default`, `OS-Level STA Shim Only`, `Library In-Process STA Shim Only`, and `Both Disabled + Runtime Toggle`).

> ⚠️ **Note on `.exe` Binaries & Bitdefender False Positive (`NativeShimRegression`):**
> All `.exe` files (`install_sta_shim.exe`, `iis_service_monitor.exe`, `NativeShimRegression_x86.exe`, `NativeShimRegression_x64.exe`, and `Tests\native\NativeShimRegression.exe`) are excluded from Git via `.gitignore` and compiled locally from C++ source when you run `sta-shim\build_shim.ps1` (or root `.\go.ps1`).
> For unknown heuristic reasons, **Bitdefender** may flag the compiled `NativeShimRegression` executable (`NativeShimRegression.exe` / `NativeShimRegression_x86.exe` / `NativeShimRegression_x64.exe`) as unsafe or suspicious. This binary is compiled directly from [`sta-shim\tests\native_regression.cpp`](./sta-shim/tests/native_regression.cpp) and only exercises local Win32 windowing, message-queue, and IAT-hook regression assertions. If Bitdefender blocks or quarantines it during local builds, add an antivirus exclusion for your working directory or for `NativeShimRegression*.exe`.

---

## 🛠️ Build, Run & Push Scripts

Each flavour uses the same simple PowerShell (`.ps1`) scripts:

### 1️⃣ Base Host Container (run from `<folder>\` or root `.\go.ps1` / `.\push.ps1`)

- 🔨 **`go.ps1`** — Build the reusable base host image (running root `.\go.ps1` first compiles `sta-shim\build_shim.ps1` and then builds all 6 flavours):

  ```powershell
  docker build -t adaskothebeast/nanoserver:<flavour> .
  ```

- ▶️ **`run.ps1`** — Run the base host container interactively on port `8080`:

  ```powershell
  docker run --rm -it -p 8080:80 adaskothebeast/nanoserver:<flavour>
  ```

  *(Uses `-p 8080:8080` for `nanoserver-aspnet-x86` and `nanoserver-aspnet-x64`.)*
- ☁️ **`push.ps1`** — Tag and push all version tags to Docker Hub (`adaskothebeast/nanoserver`).

### 2️⃣ Sample `HealthApi` Container (run from `<folder>\HealthApi\`)

- 🔨 **`HealthApi\go.ps1`** — Build the sample `HealthApi` container on top of the base image:

  ```powershell
  docker build -t adaskothebeast/nanoserver:<flavour>-sample .
  ```

- ▶️ **`HealthApi\run.ps1`** — Run the sample `HealthApi` container on port `8080`:

  ```powershell
  docker run --rm -it -p 8080:80 adaskothebeast/nanoserver:<flavour>-sample
  ```

- ✅ **Verify the health check endpoint:**

  ```powershell
  curl.exe -i --fail http://localhost:8080/health
  # HTTP/1.1 200 OK
  # {"status":"Healthy"}
  ```

### 3️⃣ COM & STA Interop Test Container (run from `<folder>\Tests\` or root `.\test.ps1`)

- 🔨 **`Tests\go.ps1`** — Build the COM & STA test container on top of the base image:

  ```powershell
  docker build -t adaskothebeast/nanoserver:<flavour>-tests .
  ```

- ▶️ **`Tests\run.ps1`** — Run the full test suite across all 4 STA shim toggle modes inside the container:

  ```powershell
  docker run --rm adaskothebeast/nanoserver:<flavour>-tests
  ```

---

## 🐳 Docker Hub Tags (`adaskothebeast/nanoserver`)

Each `push.ps1` tags and pushes a floating flavour tag plus exact, minor, and major version tags with the container revision suffix (`-v1.3.0`, `-v1.3`, `-v1`).

To bump the 3-tier version tags (`x.x.x`, `x.x`, and `x`) across all 6 subfolder `push.ps1` files at once, run **`set-version.ps1`**:

```powershell
# Updates all 6 push.ps1 files (both tag and push lines) to -v1.3.0, -v1.3, and -v1
.\set-version.ps1 -Version 1.3.0

# Optional: also bump the .NET 10 runtime version in Dockerfiles & push.ps1 files
.\set-version.ps1 -Version 1.3.0 -DotNetVersion 10.0.14
```

- **ASP.NET Core 10 (`x86` Kestrel):**
  - `adaskothebeast/nanoserver:aspnet-x86`
  - `adaskothebeast/nanoserver:aspnet-x86-10.0.14-ltsc2025-v1.3.0`
  - `adaskothebeast/nanoserver:aspnet-x86-10.0-ltsc2025-v1.3`
  - `adaskothebeast/nanoserver:aspnet-x86-10-ltsc2025-v1`
- **ASP.NET Core 10 (`x64` Kestrel):**
  - `adaskothebeast/nanoserver:aspnet-x64`
  - `adaskothebeast/nanoserver:aspnet-x64-10.0.14-ltsc2025-v1.3.0`
  - `adaskothebeast/nanoserver:aspnet-x64-10.0-ltsc2025-v1.3`
  - `adaskothebeast/nanoserver:aspnet-x64-10-ltsc2025-v1`
- **IIS 10 + ASP.NET Core 10 (`x86` OutOfProcess):**
  - `adaskothebeast/nanoserver:iis-aspnet-x86`
  - `adaskothebeast/nanoserver:iis-aspnet-x86-10.0.14-ltsc2025-v1.3.0`
  - `adaskothebeast/nanoserver:iis-aspnet-x86-10.0-ltsc2025-v1.3`
  - `adaskothebeast/nanoserver:iis-aspnet-x86-10-ltsc2025-v1`
- **IIS 10 + ASP.NET Core 10 (`x64` InProcess / OutOfProcess):**
  - `adaskothebeast/nanoserver:iis-aspnet-x64`
  - `adaskothebeast/nanoserver:iis-aspnet-x64-10.0.14-ltsc2025-v1.3.0`
  - `adaskothebeast/nanoserver:iis-aspnet-x64-10.0-ltsc2025-v1.3`
  - `adaskothebeast/nanoserver:iis-aspnet-x64-10-ltsc2025-v1`
- **IIS 10 + .NET Framework 4.8.1 (`x86`):**
  - `adaskothebeast/nanoserver:iis-netfx-x86`
  - `adaskothebeast/nanoserver:iis-netfx-x86-4.8.1-ltsc2025-v1.3.0`
  - `adaskothebeast/nanoserver:iis-netfx-x86-4.8-ltsc2025-v1.3`
  - `adaskothebeast/nanoserver:iis-netfx-x86-4-ltsc2025-v1`
- **IIS 10 + .NET Framework 4.8.1 (`x64`):**
  - `adaskothebeast/nanoserver:iis-netfx-x64`
  - `adaskothebeast/nanoserver:iis-netfx-x64-4.8.1-ltsc2025-v1.3.0`
  - `adaskothebeast/nanoserver:iis-netfx-x64-4.8-ltsc2025-v1.3`
  - `adaskothebeast/nanoserver:iis-netfx-x64-4-ltsc2025-v1`

---

## ✨ What's New in `v1.3.0` (Published to Docker Hub)

Container revision **`1.3.0`** completes the full [Roundtable review (`R01`–`R26`, `N01`–`N06`)](./docs/issues/roundtable-2026-10-29.md) and is release-qualified across all six flavours (see [`CHANGELOG.md`](./CHANGELOG.md), [`docs/roundtable-fix-validation.md`](./docs/roundtable-fix-validation.md), and [`docs/benchmarks/README.md`](./docs/benchmarks/README.md)):

- **128x–154x Faster Shimmed API-Set Queries & 2.22x Faster COM STA Init (`R17`/`R19`):** Per-module `HasAppliedState` caching, no-op `VirtualProtect` skipping, and explicit `NanoServerStaShim_Refresh()` invalidation after external IAT resets.
- **Race-Free Cross-Thread `SendMessageW` & Queue Lifetime (`R01`, `R05`–`R09`):** Strict owner-thread dispatch via `std::shared_ptr<PendingSendMessage>`, nested incoming send servicing during waits, owner-thread timers (`SetTimer`/`KillTimer`), typed `GetQueueStatus` bits, `MsgWaitForMultipleObjectsEx` wake masks (`MWMO_INPUTAVAILABLE`, `MWMO_ALERTABLE`, `MWMO_WAITALL`), descendant `HWND` filtering, persistent `WM_QUIT` re-posting, and FLS thread-exit cleanup.
- **Full Window Creation/Destruction Lifecycle & Class Atoms (`R03`, `R04`, `R10`, `N01`, `N03`):** `WM_NCCREATE`/`WM_CREATE`/`WM_DESTROY`/`WM_NCDESTROY` notifications, bounded `cbWndExtra` access (`Get/SetWindowLong[Ptr]W`), module-scoped window classes with shared/reusable atoms (`GetClassInfo[Ex]W`, `GetClassNameW`), and `CallWindowProcW` chaining.
- **Foreground IIS Service Monitor (`iis_service_monitor.exe`, `R14`):** Replaces `cmd /c ping -t` as `CMD` across all four IIS images (`iis-aspnet-x64`, `iis-aspnet-x86`, `iis-netfx-x64`, `iis-netfx-x86`), starting `WAS` and `W3SVC`, clearing SCM failure auto-restart actions, and exiting nonzero (terminating lingering `w3wp.exe` / service host processes) if either service stops or loses its host process.
- **Hardened Installer, Dockerfiles & Control Flow Guard (`R02`, `R11`–`R16`, `R18`, `R21`):** Staged `.new`/`.orig` PE patching with rollback, `--require-wow64` enforcement, `FILE_FLAG_BACKUP_SEMANTICS` for Nano Server's `System32\forwarders` ACLs, read-only `inetsrv\config\schema` handling and `RX` ACL grants in `iis-netfx-*`, `/guard:cf` across all native binaries, and ordinary-import native regression tests (`NativeShimRegression.exe`).

---

## 🔬 Deep Dive: [`AdaskoTheBeAsT.Interop.Unmanaged`](https://github.com/AdaskoTheBeAsT/AdaskoTheBeAsT.Interop.Unmanaged) & [`AdaskoTheBeAsT.Interop.COM`](https://github.com/AdaskoTheBeAsT/AdaskoTheBeAsT.Interop.COM) on Nano Server 2025

| Interop Mechanism | Status on Nano Server 2025 | Technical Details |
| :--- | :--- | :--- |
| ✅ **Plain Native DLLs via [`AdaskoTheBeAsT.Interop.Unmanaged`](https://github.com/AdaskoTheBeAsT/AdaskoTheBeAsT.Interop.Unmanaged)** | **Available for compatible headless DLLs (`x86` & `x64`)** | Standard C/C++/Delphi/PowerBasic/Fortran DLL loading (`LoadLibrary` / `GetProcAddress` / `FreeLibrary`) works natively in both 32-bit (`SysWOW64`) and 64-bit (`System32`). *(Tip: If your native DLL links against the Visual C++ Redistributable like `vcruntime140.dll` / `msvcp140.dll`, copy those CRT DLLs next to your native DLL.)* |
| ⚠️ **SxS Registration-Free COM via `CreateActCtxW` (`*.manifest`)** | **Blocked by `apisetschema.dll` (even if `sxs.dll` is copied)** | `KernelBase!CreateActCtxW` checks `IsCreateActCtxWWorkerPresent()` (`ext-ms-win-kernel32-sidebyside-l1-1-0`) in the kernel-mapped `apisetschema.dll` *before* calling `sxs.dll`, returning `ERROR_INVALID_FUNCTION (1)`. Copying Server Core's `apisetschema.dll` or `sxssrv.dll` breaks container silo boot (`0xc0370111` at `hcs::System::Start`) because Server Core's schema redirects console/windowing API sets away from Nano Server's headless `coniosrv.dll`. |
| ✅ **Registration-Free COM with `.manifest` via `DllGetClassObject` + `CoRegisterClassObject` Fallback** | **Exercised by the included COM fixtures** | Verified with `NativeCOM.dll` + `NativeCOM.manifest`: when `CreateActCtxW` returns `ERROR_INVALID_FUNCTION (1)`, parsing `<comClass clsid="{...}"/>` from the `.manifest` file, calling `DllGetClassObject` on the COM DLL, and registering the `IClassFactory` in-memory via `combase!CoRegisterClassObject(..., CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE, out cookie)` allows `new StringConcatenatorClass()` (`CoCreateInstance`) to succeed with `S_OK` on both `x86` and `x64` Nano Server without `regsvr32` or OS modifications! |
| **Single-Threaded Apartment (`STA`) and Win32 message pumping** | **Scoped headless implementation, not full Win32 parity** | Native OS-level shimming and the external managed NuGet shim are separate implementations. See the support table below and run the native and managed regression suites for your build. Exact unshimmed failure codes, including `E_NOTIMPL`, depend on the OS build and call path and are not guaranteed here. |

### Native STA shim support and limitations

This table describes the native implementation, not complete Win32 compatibility or
a performance guarantee. The native regressions must pass on the built images before
release. See [roundtable fix validation](docs/roundtable-fix-validation.md) for the
current validation status. The managed NuGet shim is a separate implementation.

The 2026-10-30 checkpoint is **release-qualified across all six images**. Native
probes, all four managed COM/STA modes (`net10.0` and `net481`), fresh IIS builds,
and `WAS`/`W3SVC` host-loss qualification pass across all six flavours; see
[roundtable fix validation](docs/roundtable-fix-validation.md) and
[performance benchmarks](docs/benchmarks/README.md).

| Area | Native shim support | Limits |
| :--- | :--- | :--- |
| Unicode classes and headless windows (R03, R10, N03) | Instance-scoped and global registration, shared/bounded/reusable name atoms, class queries, subclass chaining, creation context and lifecycle notifications, owner-only destruction | Predefined classes use a default headless procedure, not full Windows control implementations. No visible desktop or cross-process windows. |
| Window data (R04) | `Get/SetWindowLongW` use 4-byte extra-byte access; pointer variants use pointer width; accesses stay within declared `cbWndExtra` | Only the documented indexes in the export table are handled. Window property APIs (`SetPropW`, `GetPropW`, `RemovePropW`) are not implemented. |
| Sends, posts, filters and quit (R01, R07, R09, N01) | Same-thread sends dispatch directly; cross-thread sends wait for owner dispatch and service incoming sends. Pending sends cancel on destruction/exit. Filters include descendants; quit state ignores filters. | A non-pumping owner can block `SendMessageW` indefinitely, as there is no timeout. `SendMessageTimeoutW`, broadcasts and cross-process messaging are not implemented. Explicitly posted `WM_QUIT` also uses synthetic quit state. |
| Queue lifetime (R08) | Only an owner creates its queue; FLS cleanup removes its windows, timers and queued work at normal thread exit | Posting to another thread without an initialized queue fails. Forced thread termination and fiber-hosted workloads are not qualified. |
| Timers, waits and queue status (R05, R06) | Lazy owner-thread deadlines, low-priority `WM_TIMER`, replacement/cancellation, typed current/changed queue bits, wake masks and the three documented MWMO flags | Timer delivery requires pumping and is not real-time; coalescing tolerance is ignored. Wait-all uses bounded waits to keep servicing sent messages. No physical input or painting subsystem. |
| ANSI window/message APIs (N02) | Not implemented | Probe the exact engine imports and dynamic lookups before selecting these images. The few ANSI GDI exports do not imply ANSI window support. |
| GDI, icons and common controls | Minimal startup stubs for selected legacy imports | Synthetic handles, fixed text/icon approximations and no rendering or general GDI resource lifetime. Not a replacement for a GUI, VCL controls, GDI or DirectX. |
| Hook memory protection (R17, R19) | Serialized writes, no-op skipping, derived-address bounds checks and original page-protection restoration | Non-executable data pages become writable without execute access. An already-executable page may briefly become writable and executable. This is not a blanket W^X guarantee. |

The `AdaskoTheBeAsT.Interop.COM` **4.2.0** package remains pinned in the managed tests.
Its `NanoServerWindowManager` tests document that package's known deviations,
including duplicate registration and timeout fallback behavior. They are not native
shim conformance tests and are not rewritten to pretend the package has been fixed.

The native and managed shims keep separate class/window state. Select a provider
before COM/window initialization and use a new process when changing providers.
Refreshing hooks does not migrate cached COM atoms or existing windows. The mode
runners retain every test, but run each of the two process-wide provider-switching
tests in its own process. Those tests verify settings and interception, not live
state migration.

### 🎛️ Toggling the OS-Level and Library-Level Shims (for Testing & Diagnostics)

Both the container-level native STA shim (`nanoserver_sta_shim.dll`) and the `AdaskoTheBeAsT.Interop.COM` library-level shims can be independently toggled off via environment variables or at runtime via `NanoServerOptions`:

| Environment Variable / API | Target | Effect when Set to `1` / `true` |
| :--- | :--- | :--- |
| `NANOSERVER_DISABLE_STA_SHIM=1` | **OS-Level Native Shim** (`nanoserver_sta_shim.dll` in `System32` & `SysWOW64`) | Disables the container's native IAT hooks on `combase.dll`, `ole32.dll`, and `user32.dll` (and exports `NanoServerStaShim_RestoreOriginal()` / `NanoServerStaShim_Refresh()` so managed code can toggle it dynamically). |
| `ADASKO_NANOSERVER_DISABLE_STA_SHIM=1` / `NanoServerOptions.EnableStaShim = false` | **Library-Level In-Process STA Shim** (`AdaskoTheBeAsT.Interop.COM`) | Disables `AdaskoTheBeAsT.Interop.COM`'s built-in managed STA & Win32 message-queue shim (`NanoServerStaShim`). When both switches are set, the shims are disabled; do not assume a particular unshimmed HRESULT without a control on the selected OS build. |
| `ADASKO_NANOSERVER_DISABLE_SXS_FALLBACK=1` / `NanoServerOptions.EnableSxsFallback = false` | **Library-Level SxS Manifest Fallback** (`AdaskoTheBeAsT.Interop.COM`) | Disables the automatic `DllGetClassObject` + `CoRegisterClassObject` manifest fallback when `CreateActCtxW` fails on Nano Server. |

### ⚖️ Which Method Is Better: OS-Level Container Shim (`nanoserver_sta_shim.dll`) vs. `AdaskoTheBeAsT.Interop.COM` In-Process Shim?

Earlier managed-mode runs passed their selected cases, but did not assert the native contracts behind R01 to R10. The two shims operate at different layers; those historical passes do not establish full compatibility:

| Criterion | OS-Level Native Shim (`nanoserver_sta_shim.dll` in Container) | Library-Level In-Process Shim (`AdaskoTheBeAsT.Interop.COM`) |
| :--- | :--- | :--- |
| **Initialization timing** | Loaded with the patched forwarders, before managed startup in that loading path. Qualify early native COM initialization on the target image. | Loaded when the NuGet package initializes. Exact timing depends on the runtime. |
| **Call path** | Native C++ calls with no managed callback transition for the implemented message APIs. See [measured benchmarks](docs/benchmarks/README.md) for microbenchmark and mode comparisons. | Uses managed callbacks for its in-process shim. Validate callback lifetime and startup ordering for the application. |
| **Scope** | Process-wide for the hooked API/import paths, including native callers. This is not support for arbitrary Win32 APIs or every dynamically resolved import. | Applies to processes that load and enable the package. |
| **Registration-free COM** | Does not parse manifests. The application needs its own factory registration, suitable COM registration, or another fallback. | Provides a limited in-process `<file>` / `<comClass>` factory-registration fallback, not full SxS assembly binding. |
| **Installation** | Requires container system DLL changes by `install_sta_shim.exe`. | Does not require that native installer; its behavior is that of the pinned external package. |

#### Choosing a shim

Use the native shim when its supported headless contracts fit the application and
early native initialization is needed. Use the managed package for its documented
COM lifetime and manifest-fallback features. When both are loaded, verify which
implementation receives each call instead of assuming their behavior is identical.
Neither choice provides general GUI compatibility; for measured microbenchmark,
COM mode, and multi-worker STA workload comparisons across `v1.3.0`, `v1.2.0`, and
Windows Server Core `ltsc2025`, see [`docs/benchmarks/README.md`](./docs/benchmarks/README.md).

---

## 📊 Performance Benchmarks (`v1.3.0` vs. `v1.2.0` vs. Windows Server Core `ltsc2025`)

Full benchmark methodology, raw logs, and C++/PowerShell harness sources are documented in [**`docs/benchmarks/README.md`**](./docs/benchmarks/README.md) and [**`docs/roundtable-fix-validation.md`**](./docs/roundtable-fix-validation.md).

### 1. Native `nanoserver_sta_shim.dll` Microbenchmarks (`x64` & `x86`)

Caching per-module hook state (`HasAppliedState`, `R17`/`R19`), skipping no-op `VirtualProtect` syscalls, and tracking queue status bits in user mode (`R06`/`R08`) yielded dramatic hot-path speedups in `v1.3.0` while eliminating the `v1.2.0` 50 ms cross-thread `SendMessageW` wrong-thread fallback (`R01`):

| Benchmark Operation | Arch | Native Server Core (`ltsc2025`) | Nano Server `v1.2.0` | Nano Server `v1.3.0` | `v1.3.0` vs. `v1.2.0` | `v1.3.0` vs. Server Core |
| :--- | :---: | ---: | ---: | ---: | ---: | ---: |
| **`ApiSetQuery_50k`**<br>50,000 queries on shimmed `ext-ms-win-ntuser-window-l1-1-0` (`R17`/`R19`) | `x64`<br>`x86` | *(unhooked)*<br>*(unhooked)* | `40,174.82 ms`<br>`49,941.56 ms` | **`261.57 ms`**<br>**`390.65 ms`** | **153.6x faster** (`-99.35%`)<br>**127.8x faster** (`-99.22%`) | — |
| **`COM_STA_InitUninit_5k`**<br>5,000 `CoInitializeEx(COINIT_APARTMENTTHREADED)` + `CoUninitialize` cycles | `x64`<br>`x86` | `136.66 ms`<br>`319.21 ms` | `84.77 ms`<br>`64.55 ms` | **`38.13 ms`**<br>**`56.58 ms`** | **2.22x faster** (`-55.0%`)<br>**1.14x faster** (`-12.4%`) | **3.58x faster** (`-72.1%`)<br>**5.64x faster** (`-82.3%`) |
| **`WindowCreateDestroy_10k`**<br>10,000 `CreateWindowExW(HWND_MESSAGE)` + `DestroyWindow` cycles (`R03`/`R10`) | `x64`<br>`x86` | `114.05 ms`<br>`291.95 ms` | `3.60 ms`<br>`2.26 ms` | **`3.31 ms`**<br>**`5.71 ms`** | **1.09x faster**<br>`0.40x` *(4 `R03` msgs)* | **34.5x faster** (`-97.1%`)<br>**51.2x faster** (`-98.0%`) |
| **`PostPeekDispatch_200k`**<br>200,000 `PostMessageW` + `MsgWait` + `PeekMessageW` + `DispatchMessageW` (`R06`/`R08`) | `x64`<br>`x86` | `492.34 ms`<br>`576.97 ms` | `160.91 ms`<br>`234.94 ms` | **`82.48 ms`**<br>**`133.17 ms`** | **1.95x faster** (`-48.7%`)<br>**1.76x faster** (`-43.3%`) | **5.97x faster** (`-83.2%`)<br>**4.33x faster** (`-76.9%`) |
| **`SendMessageCrossThread_20k`**<br>20,000 cross-thread synchronous STA `SendMessageW` calls (`R01`/`R08`) | `x64`<br>`x86` | `197.10 ms`<br>`321.15 ms` | `124.51 ms`<br>`183.31 ms` | **`128.88 ms`**<br>**`181.92 ms`** | `0.97x`<br>**1.01x faster** | **1.53x faster** (`-34.6%`)<br>**1.77x faster** (`-43.4%`) |
| **`Concurrent10ThreadPumps_200k`**<br>10 STA threads × 20,000 messages = 200,000 messages (`R08`/`R24`) | `x64`<br>`x86` | `2,595.65 ms`<br>`2,898.08 ms` | `763.04 ms`<br>`898.08 ms` | **`816.75 ms`**<br>**`704.42 ms`** | `0.93x`<br>**1.27x faster** (`-21.6%`) | **3.18x faster** (`-68.5%`)<br>**4.11x faster** (`-75.7%`) |

### 2. Managed `AdaskoTheBeAsT.Interop.COM.Test` Suite Across All 6 Flavours

| Flavour / Target Framework | Tests | Native Server Core (`ltsc2025`) | Nano Server `v1.3.0` Mode 1 (Default: OS + Library Shim) | Nano Server `v1.3.0` Mode 2 (OS Shim Only) | Nano Server `v1.3.0` Mode 3 (Library Shim Only) | Mode 2 vs. Server Core |
| :--- | :---: | ---: | ---: | ---: | ---: | ---: |
| **`nanoserver-aspnet-x64`** (`net10.0\|x64`) | 172 | `448 ms` | `486 ms` | **`406 ms`** | `427 ms` | **1.10x faster** (`-9.4%`) |
| **`nanoserver-aspnet-x86`** (`net10.0\|x86`) | 172 | `583 ms` | `531 ms` | **`492 ms`** | `477 ms` | **1.18x faster** (`-15.6%`) |
| **`nanoserver-iis-aspnet-x64`** (`net10.0\|x64`) | 172 | `448 ms` | `450 ms` | **`409 ms`** | `389 ms` | **1.10x faster** (`-8.7%`) |
| **`nanoserver-iis-aspnet-x86`** (`net10.0\|x86`) | 172 | `583 ms` | `561 ms` | **`532 ms`** | `487 ms` | **1.10x faster** (`-8.7%`) |
| **`nanoserver-iis-netfx-x64`** (`net481\|x64`) | 173 | `720 ms` | `842 ms` | **`824 ms`** | `830 ms` | `0.87x` (`+104 ms`) |
| **`nanoserver-iis-netfx-x86`** (`net481\|x86`) | 173 | `599 ms` | `683 ms` | **`665 ms`** | `681 ms` | `0.90x` (`+66 ms`) |

### 3. Real-World 200-Test `AhuNew` Suite (32-Bit Delphi `CoilLib` STA Worker Pool & ASP.NET Core Minimal API)

Running all 200 engineering and interop tests directly via the 32-bit `.NET 10.0.14` host (`C:\Program Files (x86)\dotnet\dotnet.exe`) inside `--isolation=process --cpus 10` demonstrates full execution-speed parity with Native Windows Server Core (`ltsc2025`) at ~13% of the compressed image size:

| Benchmark Metric | Nano Server `v1.3.0` (`aspnet-x86`) | Nano Server `v1.2.0` (`aspnet-x86`) | Original Server Core (`ltsc2025`) | Server Core .NET 10 SDK (`ltsc2025`) |
| :--- | ---: | ---: | ---: | ---: |
| **Total 200-Test Suite (Cold / Warm)** | **`14.56 s` / `14.37 s`** | `14.35 s` / `14.14 s` | `14.70 s` / `14.85 s` | `14.60 s` / `14.59 s` |
| **10-Worker STA Pool — Water (`200` concurrent)** | **`285.51 ms` (`700.5 calc/s`, `4.62x`)** | `299.55 ms` (`667.7 calc/s`, `4.31x`) | `305.36 ms` (`655.0 calc/s`, `4.18x`) | `284.56 ms` (`702.8 calc/s`, `4.59x`) |
| **10-Worker STA Pool — Freon (`200` concurrent)** | **`338.23 ms` (`591.3 calc/s`, `5.00x`)** | `383.64 ms` (`521.3 calc/s`, `3.93x`) | `358.21 ms` (`558.3 calc/s`, `4.18x`) | `301.07 ms` (`664.3 calc/s`, `5.51x`) |
| **10-Worker STA Pool — Condensing (`200` concurrent)** | **`259.79 ms` (`769.8 calc/s`, `4.87x`)** *(warm)* | `251.84 ms` (`794.1 calc/s`, `5.11x`) | `273.21 ms` (`732.1 calc/s`, `5.23x`) | `261.07 ms` (`766.1 calc/s`, `5.09x`) |
| **10-Worker STA Pool — Glycol (`40` concurrent)** | **`176.18 ms` (`227.0 calc/s`, `4.73x`)** | `193.82 ms` (`206.4 calc/s`, `3.96x`) | `173.85 ms` (`230.1 calc/s`, `4.24x`) | `228.14 ms` (`175.3 calc/s`, `3.28x`) |
| **ASP.NET Core Minimal API — 1-Worker (`120` HTTP reqs)** | **`807.04 ms` (`148.7 req/s`, `6.73 ms/req`)** | `819.80 ms` (`146.4 req/s`, `6.83 ms/req`) | `807.16 ms` (`148.7 req/s`, `6.73 ms/req`) | `811.20 ms` (`147.9 req/s`, `6.76 ms/req`) |
| **ASP.NET Core Minimal API — 10-Worker (`120` HTTP reqs)** | **`124.09 ms` (`967.0 req/s`, `1.03 ms/req`)** | `118.30 ms` (`1014.4 req/s`, `0.99 ms/req`) | `124.84 ms` (`961.2 req/s`, `1.04 ms/req`) | `122.25 ms` (`981.6 req/s`, `1.02 ms/req`) |

---

## 🧰 The Complete `AdaskoTheBeAsT.Interop.*` Ecosystem for Native & COM Workloads

When you integrate mission-critical native code—whether C, C++, Delphi, Fortran, PowerBasic, or legacy 32-bit/64-bit COM/ActiveX components—into a modern cloud-native .NET service, raw `DllImport` or `new MyComClass()` is only 10% of the problem. The remaining 90% is **deterministic lifetime management, thread affinity, STA message pumping, session recycling, and isolation**.

The four `AdaskoTheBeAsT.Interop.*` libraries form a layered architecture designed to solve each of those failure modes cleanly across `.NET 8`, `.NET 9`, `.NET 10`, and `.NET Framework 4.7.2–4.8.1`:

```
┌───────────────────────────────────────────────────────────────────────────────────┐
│  Application / ASP.NET Core / Worker Service (DI, Hosting, OpenTelemetry)         │
├───────────────────────────────────────────────────────────────────────────────────┤
│  1. AdaskoTheBeAsT.Interop (Execution / DI / Hosting)                             │
│     • ExecutionWorker<TSession> & ExecutionWorkerPool<TSession> (UseStaThread)    │
│     • Pooled IValueTaskSource<T> (ExecuteValueAsync), LeastQueued / RoundRobin    │
│     • Session recycling (MaxOperationsPerSession & RecycleSessionAfterExecution)  │
├─────────────────────────────────────────┬─────────────────────────────────────────┤
│  2. AdaskoTheBeAsT.Interop.Threading    │  3. AdaskoTheBeAsT.Interop.COM          │
│     • SingleThreadedApartmentTask-      │     • Executor.Execute (STA/MTA + SxS)  │
│       Scheduler & RunCooperativeAsync   │     • ComObjectHandle<T> (LIFO release) │
│     • StaYield (bounded message pump    │     • Registration-free SxS + Nano      │
│       inside long native loops)         │       Server CoRegisterClassObject      │
│     • MutexHelper.RunInMutex (cross-    │     • Built-in NanoServerStaShim +      │
│       process serialization)            │       bounded 256-msg Win32 pump        │
├─────────────────────────────────────────┴─────────────────────────────────────────┤
│  4. AdaskoTheBeAsT.Interop.Unmanaged                                              │
│     • UnmanagedLibrary & SafeLibraryHandle (LoadLibraryEx / NativeLibrary)        │
│     • Typed delegate binding (GetUnmanagedFunction<T>) & IL Calli raw thunks      │
│     • DelegatePin (PinConcreteDelegate / PinDelegate for reverse callbacks)       │
└───────────────────────────────────────────────────────────────────────────────────┘
```

### 1. [`AdaskoTheBeAsT.Interop.Unmanaged`](https://github.com/AdaskoTheBeAsT/AdaskoTheBeAsT.Interop.Unmanaged) — Native DLL Loader, `calli` Thunks & Callback Pinning

Use this when you have plain native DLLs (`.dll` / `.so` / `.dylib`) whose paths come from configuration, whose exports vary by customer SDK version, or that need to invoke managed callbacks without GC-collected-delegate crashes:

- **Deterministic Module Ownership (`UnmanagedLibrary` / `SafeLibraryHandle`):** Wraps `LoadLibraryExW` (with explicit `LoadLibraryFlags` search policies on Windows) and `FreeLibrary` in a deterministic `SafeHandle` scope instead of permanently pinning DLLs in the process via static `DllImport`.
- **Typed Function Pointers, Raw `calli` Thunks & Optional Probing:** `GetUnmanagedFunction<TDelegate>("ExportName")` binds native exports to strongly-typed `[UnmanagedFunctionPointer]` delegates (returning `null` when an optional symbol is absent in an older SDK), while `GetRawUnmanagedFunction<TDelegate>` emits a cached `DynamicMethod` using `OpCodes.Calli` (`CreateRawCallThunk`) for direct indirect calls.
- **Reverse-Callback Keep-Alive (`DelegatePin` via `PinConcreteDelegate` / `PinDelegate`):** The #1 bug in C# $\leftrightarrow$ C/C++ interop is passing a delegate to native code and having the GC collect the managed delegate before the native callback finishes (because the JIT considers the last C# reference to have ended before the native call returns). `DelegatePin` couples the native `IntPtr` function pointer with `GC.KeepAlive` in a `using` scope, and even emits a dynamic `[UnmanagedFunctionPointer]` proxy assembly (`CreateProxyDelegateType`) when you pass generic `Func<...>` or `Action<...>` delegates.

### 2. [`AdaskoTheBeAsT.Interop.COM`](https://github.com/AdaskoTheBeAsT/AdaskoTheBeAsT.Interop.COM) — Deterministic COM Lifetime, Registration-Free SxS & Nano Server Shims

Use this whenever you touch COM (`IDispatch`, `IUnknown`, VB6/Delphi/ATL/MFC in-proc servers) and want zero leaked RCWs, zero `regsvr32` during deployment, and full Nano Server compatibility:

- **Deterministic LIFO RCW Cleanup (`ComObjectHandle<T>` & `ComObjectHandleContext`):** Never rely on the .NET finalizer thread to release COM objects (which causes cross-thread marshaling deadlocks or hung native DLL unloads). `ComObjectHandleContext` tracks every intermediate COM reference (`Workbooks` $\rightarrow$ `Workbook` $\rightarrow$ `Worksheets` $\rightarrow$ `Range`) and calls `Marshal.FinalReleaseComObject` in strict **reverse (LIFO)** creation order.
- **Registration-Free COM (`Executor.Execute` / `ActivationContext` + Nano Server Fallback):** Activates COM components directly from a side-by-side `.manifest` file (`CreateActCtxW` on Desktop/Server Core, or automatic XML manifest parsing + `DllGetClassObject` + `CoRegisterClassObject` on Nano Server).
- **Bounded Message Pumping (`pumpPendingMessages: true`):** Drains up to `256` queued Win32 messages (`PeekMessageW` / `TranslateMessage` / `DispatchMessageW`) after each execution so STA COM objects that rely on hidden windows or `WM_TIMER` / `WM_USER` notifications never starve or livelock under message floods.

### 3. [`AdaskoTheBeAsT.Interop.Threading`](https://github.com/AdaskoTheBeAsT/AdaskoTheBeAsT.Interop.Threading) — Dedicated STA Schedulers, Cooperative Pumping & Cross-Process Mutexes

Use this when a COM component or thread-affine native library must live across multiple requests on the **exact same OS thread**, or when multiple processes share a single-instance native engine:

- **`SingleThreadedApartmentTaskScheduler` & `SingleThreadedApartmentTask`:** Pins an STA-initialized background thread (`CoInitializeEx(COINIT_APARTMENTTHREADED)`) and serializes synchronous delegates onto it with bounded queue admission (`MaximumPendingWorkItems`) and per-item deadlines (`DefaultWorkItemTimeout`).
- **Cooperative Cancellation & Message Pumping (`RunCooperativeAsync` + `StaYield`):** Long synchronous loops on an STA thread freeze COM marshaling unless the thread pumps its message queue. `staYield.Occasionally()`, `staYield.Sleep(ms, token)`, and `staYield.SpinUntil(...)` use overflow-safe `Stopwatch.GetTimestamp()` math to periodically pump bounded batches of Win32 messages (`NativeMethods.PumpPendingMessages()`) while checking linked caller/timeout/shutdown `CancellationToken`s.
- **Cross-Process Serialization (`MutexHelper.RunInMutex`):** Many legacy native DLLs write to fixed temp files, shared memory segments, or hardware ports. `MutexHelper` serializes work across processes with explicit `Global\` / `Local\` scope, ACL security, and abandoned-mutex policies.

### 4. [`AdaskoTheBeAsT.Interop`](https://github.com/AdaskoTheBeAsT/AdaskoTheBeAsT.Interop) (`AdaskoTheBeAsT.Interop.Execution`) — Production Worker Pools, Session Recycling, DI & OpenTelemetry

Use this at the service architecture layer (ASP.NET Core / Worker Services) to turn fragile single-threaded native engines or COM sessions into high-throughput, self-healing, observable worker pools:

- **`ExecutionWorker<TSession>` & `ExecutionWorkerPool<TSession>`:** Each worker owns a dedicated OS thread (`UseStaThread = true` on Windows) and a native `TSession` created by your `IExecutionSessionFactory<TSession>`. Requests are dispatched across the pool via `LeastQueuedWorkerScheduler` or `RoundRobinWorkerScheduler` with lock-free compare-and-swap (`CAS`) bounded queue admission (`QueueCapacity`).
- **Allocation-Friendly `ExecuteValueAsync`:** Pools internal `IValueTaskSource<T>` work items (backed by `ManualResetValueTaskSourceCore<T>`) on `IExecutionDispatcher<TSession>` so high-frequency native calls avoid per-request `Task` allocations.
- **Self-Healing Session Recycling (`MaxOperationsPerSession` & `ExecutionRequestOptions.RecycleSessionAfterExecution`):** Native C++ and COM components are notorious for slow memory leaks or corrupted internal state after an error. `ExecutionWorker<TSession>` automatically tears down and recreates `TSession` after $N$ operations or on demand—and includes the recycle teardown in the caller's awaitable lifecycle so cleanup failures are never silently swallowed.
- **First-Class DI, Hosting & OpenTelemetry:** `AddExecutionWorkerPool<TSession>()` and `AddExecutionWorkerHostedService<TSession>()` wire the pool into `Microsoft.Extensions.Hosting` (with `Drain` or `CancelPending` shutdown policies) and emit OpenTelemetry spans (`ActivitySource`) and metrics (`Meter`, including leak-free `WeakReference`-tracked `execution.worker.queue_depth`).

### 🧩 End-to-End Blueprint: Combining the Ecosystem in One Service

Here is how these libraries compose when hosting a thread-affine, registration-free STA COM or native calculation engine inside an ASP.NET Core service on Nano Server:

```csharp
using System;
using System.Threading;
using System.Threading.Tasks;
using AdaskoTheBeAsT.Interop.COM;
using AdaskoTheBeAsT.Interop.Execution;
using AdaskoTheBeAsT.Interop.Threading;
using AdaskoTheBeAsT.Interop.Unmanaged;

// 1. Define the per-thread native/COM session owned by each STA worker thread:
public sealed class NativePricingSession : IDisposable
{
    private readonly UnmanagedLibrary _helperDll;
    private readonly StaYield _staYield = new(intervalMs: 15);
    public string ComDllPath { get; }
    public string ManifestPath { get; }

    public NativePricingSession(string helperDllPath, string comDllPath, string manifestPath)
    {
        // Load companion C/C++ helper DLL with explicit lifecycle ownership:
        _helperDll = new UnmanagedLibrary(helperDllPath);
        ComDllPath = comDllPath;
        ManifestPath = manifestPath;
    }

    public decimal CalculatePrice(decimal baseAmount, CancellationToken ct)
    {
        decimal output = 0m;

        // Activate registration-free COM via manifest (uses CoRegisterClassObject on Nano Server)
        // and pump up to 256 pending Win32 messages on completion:
        var result = Executor.Execute(
            ComDllPath,
            ManifestPath,
            () =>
            {
                using var ctx = new ComObjectHandleContext();
                var engine = ctx.Register(new LegacyPricingLib.PricingEngineClass());

                // Keep the STA message queue responsive during multi-step calculation:
                ct.ThrowIfCancellationRequested();
                _staYield.Occasionally();
                output = engine.Compute(baseAmount);
            },
            pumpPendingMessages: true);

        if (!result.Success)
        {
            throw new InvalidOperationException("Native COM pricing failed.", result.Exception);
        }

        return output;
    }

    public void Dispose() => _helperDll.Dispose();
}

// 2. Wire into ExecutionWorkerPool<NativePricingSession> with STA threads & auto-recycling:
//    - UseStaThread = true (runs on Nano Server STA via nanoserver_sta_shim.dll / NanoServerStaShim)
//    - MaxOperationsPerSession = 5_000 (recycles native session every 5,000 calls to prevent native heap bloat)
//    - Pooled ValueTask dispatch via ExecuteValueAsync
```

---

## 🧠 Windows Internals Masterclass: How Nano Server Is Built & How We Engineered Around Its Limits

*Written for systems programmers and senior .NET/C++ engineers who want to understand what actually happens inside `ntdll.dll`, `KernelBase.dll`, `combase.dll`, and the Windows PE loader.*

### 1. How Modern Windows Is Architected: MinWin, OneCore & `PEB->ApiSetMap`

If you grew up on Windows NT 4.0, Windows 2000, or Windows Server 2003, you learned a simple mental model:
- `kernel32.dll` implements base OS services (files, threads, modules, activation contexts).
- `user32.dll` and `gdi32.dll` implement windowing and graphics by issuing system calls into the kernel-mode driver `win32k.sys`.
- `ole32.dll` implements COM and OLE.

Starting with Windows 7 and culminating in Windows 10/11 and Windows Server 2016–2025 (**OneCore**), Microsoft inverted that dependency graph. Legacy DLLs like `kernel32.dll`, `user32.dll`, and `ole32.dll` became thin **umbrella/forwarder facades**, while the real implementations moved into lower-level host binaries (`KernelBase.dll`, `combase.dll`, `win32u.dll`, `ntdll.dll`).

To decouple callers from physical DLL filenames across Desktop, Server Core, Xbox, HoloLens, and Nano Server, Windows introduced **API Sets**—virtual DLL names that start with `api-ms-win-*` or `ext-ms-win-*`:

```
┌────────────────────────────────────────────────────────────────────────────┐
│  Caller Binary (e.g., combase.dll or KernelBase.dll)                       │
│  Imports virtual contract: ext-ms-win-rtcore-ntuser-window-ext-l1-1-0.dll  │
└────────────────────────────────────┬───────────────────────────────────────┘
                                     │
                                     ▼
┌────────────────────────────────────────────────────────────────────────────┐
│  ntdll.dll Loader (LdrpResolveDllName / ApiSetResolveToHost)               │
│  Reads PEB->ApiSetMap (mapped from C:\Windows\System32\apisetschema.dll)   │
└─────────────────┬────────────────────────────────────────┬─────────────────┘
                  │                                        │
     On Desktop / Server Core                       On Nano Server 2025
     (Host target = user32.dll)                     (Host target = EMPTY "")
                  │                                        │
                  ▼                                        ▼
┌───────────────────────────────────┐    ┌───────────────────────────────────┐
│  Binds IAT slot to user32.dll ➡️   │    │  IsRegisterClassWPresent() == 0   │
│  win32u.dll ➡️ win32kfull.sys     │    │  Returns ERROR_INVALID_FUNCTION   │
│  (Ring-0 Kernel Syscall)          │    │  or E_NOTIMPL (0x80004001)        │
└───────────────────────────────────┘    └───────────────────────────────────┘
```

Notice the critical distinction between the two prefixes:
- **`api-ms-win-*` (Core API Sets):** Architectural contracts guaranteed to exist on **every** OneCore OS SKU.
- **`ext-ms-win-*` (Extension API Sets):** Optional contracts for subsystems that may be physically stripped from headless SKUs like Nano Server. When Microsoft links `KernelBase.dll` or `combase.dll` against an `ext-ms-win-*` contract, the linker generates a companion query helper—such as `IsCreateActCtxWWorkerPresent()` or `IsRegisterClassWPresent()`—and points the default Import Address Table (IAT) thunk at an internal stub that returns `0` (`FALSE`) unless `PEB->ApiSetMap` maps that extension contract to a real host DLL.

On Nano Server, `\Windows\System32\forwarders\user32.dll` and `\Windows\System32\forwarders\ole32.dll` are tiny forwarder DLLs, and `C:\Windows\System32\apisetschema.dll` intentionally maps the GUI (`ext-ms-win-rtcore-ntuser-*`) and Side-by-Side (`ext-ms-win-kernel32-sidebyside-*`) extension contracts to **zero host modules**.

### 2. Why Naively Copying DLLs from Server Core Fails (and Crashes the Container Silo)

Every experienced engineer's first instinct when hitting a missing feature on Nano Server is: *"Just copy the missing DLLs (`sxs.dll`, `user32.dll`, `apisetschema.dll`) from `mcr.microsoft.com/windows/servercore:ltsc2025`!"*

Here is why that fails at three distinct levels of the OS:

1. **Copying `sxs.dll` alone does nothing:**
   When you call `CreateActCtxW` in `KernelBase.dll`, the very first instruction block calls `IsCreateActCtxWWorkerPresent()`. That function checks whether `ext-ms-win-kernel32-sidebyside-l1-1-0.dll` resolves in `PEB->ApiSetMap`. Because Nano Server's `apisetschema.dll` has an empty host entry for that contract, `KernelBase!CreateActCtxW` immediately sets `LastError = ERROR_INVALID_FUNCTION (1)` and returns `INVALID_HANDLE_VALUE` **without ever attempting to load `sxs.dll`**. And even if you call `sxs.dll!CsrCreateActivationContext` directly, `sxs.dll` sends an ALPC message to the CSRSS (`csrss.exe`) server DLL `sxssrv.dll`, which is not loaded in Nano Server's subsystem process.
2. **Copying Server Core's `apisetschema.dll` bricks the container (`0xc0370111`):**
   If you replace Nano Server's `C:\Windows\System32\apisetschema.dll` with Server Core's `apisetschema.dll`, the Host Compute Service (`hcs::System::Start`) aborts container startup with `0xc0370111`. Why? Because Nano Server uses a lightweight headless console subsystem (`coniosrv.dll`) wired through Nano-specific API set mappings in `apisetschema.dll`. Server Core's `apisetschema.dll` redirects those console/windowing contracts to desktop CSRSS modules (`winsrv.dll` / `win32k.sys`) that do not exist in Nano Server's kernel silo, killing `csrss.exe` during boot.
3. **Copying `user32.dll` / `win32u.dll` from Server Core cannot work without `win32k.sys`:**
   Full `user32.dll` delegates window station, desktop, class registration (`NtUserRegisterClassExWOW`), window creation (`NtUserCreateWindowEx`), and message queues (`NtUserPeekMessage`, `NtUserMessageCall`) to `win32u.dll`, which executes `syscall` instructions into the Ring-0 kernel driver `win32k.sys` / `win32kfull.sys`. Nano Server runs in a headless server silo without a Win32k GUI subsystem initialized for the session.

### 3. Breakthrough #1: How We Replaced Windows SxS (`CreateActCtxW`) Using `combase.dll`'s Internal Class Table

To make Registration-Free COM (`NativeCOM.dll` + `NativeCOM.manifest`) work without `CreateActCtxW` or `regsvr32`, we examined how `combase.dll!CoCreateInstance` actually resolves a `CLSID` inside Microsoft's COM runtime.

When an application calls `CoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, ppv)`, `combase.dll` searches its catalog providers in a strict priority order:
1. **In-Process Registered Class Table (`CClassCache` / `CoRegisterClassObject`):** An in-memory table inside `combase.dll` of `IClassFactory` pointers registered by the current process.
2. **Thread Activation Context Stack (`TEB->ActivationContextStackPointer`):** The SxS manifest table populated by `CreateActCtxW` + `ActivateActCtx` (missing on Nano Server).
3. **Windows Registry (`HKCR\CLSID\{...}\InprocServer32`):** Global or per-user registry lookup populated by `regsvr32`.

Crucially, **step 1 (`CoRegisterClassObject`) runs *before* step 2 (SxS) and step 3 (Registry), and the included fixture uses `CoRegisterClassObject` from Nano Server's `combase.dll`.**

Historically, developers think of `CoRegisterClassObject` as an API for out-of-process (`CLSCTX_LOCAL_SERVER`) `.exe` servers. However, `combase.dll` fully supports registering `CLSCTX_INPROC_SERVER` with `REGCLS_MULTIPLEUSE` in the current process's class table!

Therefore, when `CreateActCtxW` fails on Nano Server with `ERROR_INVALID_FUNCTION (1)`, `AdaskoTheBeAsT.Interop.COM`'s `ActivationContext` executes a surgical user-mode replacement for SxS:

```
NativeCOM.manifest (XML)
  │
  ├─► 1. Parse <file name="NativeCOM.dll"> and <comClass clsid="{...}"/>
  │      (Hardened XmlReader: DtdProcessing.Prohibit, XmlResolver = null)
  │
  ├─► 2. Load native COM binary via LoadLibraryExW("...\NativeCOM.dll")
  │      and resolve its standard COM export: GetProcAddress("DllGetClassObject")
  │
  ├─► 3. For each CLSID in the manifest:
  │      Call DllGetClassObject(clsid, IID_IClassFactory, out IClassFactory pFactory)
  │
  ├─► 4. Register pFactory into combase.dll's in-process catalog:
  │      CoRegisterClassObject(ref clsid, pFactory, CLSCTX_INPROC_SERVER,
  │                            REGCLS_MULTIPLEUSE, out uint cookie)
  │
  ├─► 5. Caller executes standard COM activation (new MyComClass() / CoCreateInstance):
  │      combase.dll finds clsid in step #1 (in-proc table) and calls
  │      pFactory->CreateInstance(...) — 0 registry keys, 0 SxS syscalls!
  │
  └─► 6. On scope exit:
         CoRevokeClassObject(cookie) + Marshal.Release(pFactory)
```

### 4. Breakthrough #2: How We Engineered a User-Mode Win32 Window & Message-Queue Engine for COM STA

Why does COM STA initialization need window/message support that stock Nano Server omits? The exact failure code depends on the OS build and call path.

Because a Single-Threaded Apartment (STA) is, by definition in the COM specification, **a thread whose incoming cross-apartment calls are serialized through a Win32 window message queue**. When the first STA thread initializes in `combase.dll` (`InitMainThreadWnd` / `RegisterOleWndClass`), `combase.dll` calls:
- `IsRegisterClassWPresent()` (checking `ext-ms-win-rtcore-ntuser-window-ext-l1-1-0`)
- `RegisterClassExW(&wndClass)` for `"OleMainThreadWndClass"`
- `CreateWindowExW(0, L"OleMainThreadWndClass", L"OleMainThreadWndName", WS_DISABLED, ..., HWND_MESSAGE, ...)`

When another thread makes a COM call into that STA apartment, `combase.dll` posts or sends a custom Win32 message to that hidden `HWND_MESSAGE` window, and the STA thread's message loop (`PeekMessageW` / `GetMessageW` / `DispatchMessageW` / `MsgWaitForMultipleObjectsEx`) dispatches the message into `combase!ThreadWndProc`, which executes the target COM vtable method on the owning STA thread.

Notice the profound architectural insight here:
> **`combase.dll` does NOT need pixels, a monitor, GDI, DirectX, or a kernel-mode display driver (`win32k.sys`).** It only needs:
> 1. A handle table mapping synthetic `HWND` values to an owning `ThreadId`, class name, `WNDPROC` function pointer, `GWLP_USERDATA`, and declared extra window bytes. The native shim does not implement a window property bag.
> 2. A per-thread FIFO message queue (`MSG`) backed by a real Win32 manual-reset `EVENT` handle (`CreateEventW`) so `PostMessageW` / `PostThreadMessageW` can wake a sleeping `MsgWaitForMultipleObjectsEx` or `GetMessageW` call.
> 3. Synchronous same-thread dispatch (`SendMessageW` / `DispatchMessageW`) that directly invokes the registered `WNDPROC` callback (`lpfnWndProc(hwnd, msg, wParam, lParam)`).

The native C++ shim and the external C# package both provide headless window/message implementations. Their supported contracts and known deviations differ; the table above describes the native shim.

#### How the OS-level shim is installed and loaded

1. **Container build:** `install_sta_shim.exe` refuses a host with
   `System32\win32u.dll`. It preflights the payload and the `user32.dll`,
   `ole32.dll`, `gdi32.dll` and `comctl32.dll` forwarders for each selected
   architecture before changing system files. The five images that install
   WOW64 pass `--require-wow64` (`/require-x86` is an alias); x64 Kestrel does
   not require an x86 payload.
2. **File replacement:** the installer stages PE changes in `.new`, flushes and
   verifies them, moves the untouched original to `.orig`, and restores it if
   replacement fails. It patches API-query imports and adds missing export
   forwarders to `nanoserver_sta_shim.<ExportName>`. Both the forwarder and
   system-directory copies are re-read and checked. Each required failure
   returns nonzero. KnownDLLs entries for `user32`, `ole32` and `gdi32` are
   removed only after the files have passed verification; already-absent
   values count as success. Installation is not a transaction across all files;
   a failed Docker build must not be published.
3. **Process load:** processes that load a patched forwarder also load
   `nanoserver_sta_shim.dll`. The shim redirects API-query and delay-load slots
   in `combase.dll`, `user32.dll` and `ole32.dll`. The presence functions are
   `Shim_ApiSetQueryApiSetPresence` and `Shim_ApiSetQueryApiSetPresenceEx`, not
   an unconditional replacement for every API namespace. Cache discovery is
   range-checked; a captured module is rewritten only when its enable state
   changes or a previous write failed.
4. **Execution:** process-local window, class, timer and queue tables use
   `CRITICAL_SECTION` locks. Window procedures run outside the window lock,
   on the owner thread. The thread registers its own queue for FLS cleanup.
   `NanoServerStaShim_Refresh()` applies an environment-switch change;
   `NanoServerStaShim_RestoreOriginal()` restores captured slots.

Removing the KnownDLLs entries is an intentional part of loading the patched disk
copies, not a general-purpose Windows compatibility layer. Do not run this installer
on a workstation or Server Core host, or transplant Server Core's API-set schema.

### 5. Breakthrough #3: Solving the `0xc0370111` (`hcs::System::Start`) Post-DISM Servicing Race Condition in Docker Builds

If you have ever built Windows container images that install Features on Demand (`DISM /Online /Add-Capability /CapabilityName:Microsoft.NanoServer.Datacenter.WOWSupport /NoRestart`), you may have encountered a mysterious, intermittent `0xc0370111` (`ERROR_HV_COMMUNICATION` / `hcs::System::Start`) crash on a subsequent `RUN` instruction.

By inspecting `C:\Windows\Logs\CBS\CBS.log` and `C:\Windows\WinSxS\pending.xml` inside the raw Docker `windowsfilter` layers, we uncovered the exact OS-level race condition:
1. Installing `Microsoft.NanoServer.Datacenter.WOWSupport` stages `Package_for_RollupFix` and exits with code **`3010` (`ERROR_SUCCESS_REBOOT_REQUIRED`)**, leaving a **7.67 MB `C:\Windows\WinSxS\pending.xml`** and setting `ExecuteState = CbsExecuteStateResolvePending` for the next container boot.
2. When the `RUN C:\install\install_features.cmd` container exits, `TiWorker.exe` runs `Poqexec.exe` (primitive file/registry operations), and the next `RUN` instruction acts as the post-servicing "reboot".
3. However, if that next `RUN` instruction is a fast check (such as `RUN if exist C:\Windows\System32\inetsrv\appcmd.exe (echo IIS installed)`), `cmd.exe` exits in **under 10 milliseconds** and Docker immediately initiates container shutdown (`PRESHUTDOWN`) **while `TrustedInstaller` is still booting to resolve `pending.xml`** (`IAdvancedInstallerAwareStore_ResolvePendingTransactions`).
4. Inside `CBS.log`, `TrustedInstaller` logs:
   - `Startup: A system shutdown was initiated while waiting for startup to complete`
   - `Begin executing advanced installer phase 31 ... Installer name: 'Mof'`
   - `Waiting for IsWinMgmtServiceReady to be ready`
5. Because the Service Control Manager (`services.exe`) is already shutting down the container, `Winmgmt` (WMI) can never start, `TiWorker.exe` hangs waiting for `IsWinMgmtServiceReady` until the shutdown timeout expires, and Docker commits a **half-serviced, corrupted OS layer** (before Phase 38 finishes servicing `Microsoft-Windows-LSA` and `Microsoft-Windows-Wininit`). Every subsequent `RUN` step on top of that layer then crashes during boot with `0xc0370111`!

**The Deterministic Fix:**
In every Dockerfile that installs Features on Demand, the `RUN` instruction immediately following `install_features.cmd` / `install_capabilities.cmd` executes:
```dockerfile
RUN DISM /Online /Get-Capabilities >nul `
    && (net stop TrustedInstaller >nul 2>&1 & del /F /Q C:\Windows\Logs\CBS\*.log C:\Windows\Logs\DISM\*.log >nul 2>&1) `
    && (if exist C:\Windows\WinSxS\pending.xml (echo CBS pending.xml still present && exit /b 1))
```
Opening a CBS session via `DISM /Online /Get-Capabilities` while the container is **running normally (prior to shutdown)** allows `Winmgmt` and `TrustedInstaller` to start cleanly and synchronously complete all 115 pending advanced installer operations, delete `pending.xml`, and clean up the ~98 MB `CBS.log`/`DISM.log` files before the `RUN` step exits—eliminating the `0xc0370111` race condition and reducing image layer disk usage by over **1.2 GB**.

---

## Security boundaries and compatibility qualifications

The native shim runs inside the application process. That avoids implementing a
desktop GUI subsystem, but it does not make arbitrary native code safe. The shim
shares the process's permissions and address space; a memory-safety bug can still
compromise the process.

- Synthetic windows and message queues are process-local. They are not a security
  boundary between components in the same process.
- Hook writes are serialized and restore the original protection. The support
  table above describes the temporary writable-page behavior; no blanket W^X
  compliance is claimed.
- The native build enables Control Flow Guard (`/guard:cf`). This does not prove
  memory safety or replace security testing.
- The installer modifies container system DLLs and KnownDLLs. Its host check is a
  guard, not a sandbox. Build only in disposable Nano Server containers and do
  not publish a layer after any installer or servicing failure.
- The manifest fallback belongs to the external managed package. It handles
  in-process COM `<file>` / `<comClass>` activation, not full SxS assembly binding,
  and registers factories process-wide rather than using per-thread activation
  contexts. Validate applications that load competing implementations of one CLSID.
- Base image tags still float, and downloaded runtime/ANCM payloads do not yet
  have a pinned hash manifest. The roundtable lists this as separate R21
  hardening work, not a completed security guarantee.


