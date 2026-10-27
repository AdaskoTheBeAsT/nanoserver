# 🚀 Windows Nano Server 2025 — IIS 10, ASP.NET Core 10 & .NET Framework 4.8.1 (`x86` & `x64`)

**Run the "impossible" .NET workloads on Windows Nano Server 2025 (`ltsc2025`).**

This repository provides production-ready, reusable **Nano Server 2025 base images** (and sample `HealthApi` apps) published to a single unified Docker Hub repository (**`adaskothebeast/nanoserver`**) for the exact .NET scenarios where Microsoft's official images normally force you onto massive **5–8 GB Windows Server Core** containers:

- ⚡ **32-bit (`x86`) & 64-bit (`x64`) ASP.NET Core 10** — Self-hosted **Kestrel** (with built-in COM STA & Win32 message-queue shim) & **IIS 10 (`OutOfProcess` / `InProcess`)**
- 🌐 **64-bit (`x64`) ASP.NET Core 10 on IIS 10** — **`InProcess`** & **`OutOfProcess`** via ANCM V2
- 🏛️ **32-bit (`x86`) & 64-bit (`x64`) Full .NET Framework 4.8.1 (`net481`) on IIS 10** — Full `System.Web` / ASP.NET Web API / MVC / WebForms

---

## 🏆 The Backstory: Asking Microsoft for 32-bit Nano Server ([`microsoft/Windows-Containers#118`](https://github.com/microsoft/Windows-Containers/issues/118))

Back in 2022, when Microsoft's Windows Containers team asked in [**`microsoft/Windows-Containers#118` (*Add 32-bit x86 Support for Nanoserver*)**](https://github.com/microsoft/Windows-Containers/issues/118) what real-world workloads needed 32-bit (`x86`) support on Nano Server, I ([**@AdaskoTheBeAsT**](https://github.com/AdaskoTheBeAsT)) made the case for the .NET ecosystem:

> *"In industries like HVAC, component manufacturers for water heaters, coolers, fans, etc. provide their calculation libraries as `x86`-only DLLs — plain native DLLs and COM libraries written in VB, C++, Delphi, or even PowerBasic. Putting a modern .NET Web API / worker facade on top of them inside slim Windows containers scaled across a message bus or load balancer is the natural modernization path — but being forced onto a ~5 GB Windows Server Core image is way too heavy for a simple worker or API container."*

Microsoft listened and introduced **Nano Server Features on Demand** (`Microsoft.NanoServer.Datacenter.WOWSupport` and `Microsoft.NanoServer.IIS`) in **Windows Server 2025 (`ltsc2025`)** ([announcement blog](https://techcommunity.microsoft.com/blog/containers/discover-the-new-era-of-windows-server-2025-nano-server-containers/4413060)).

However, turning those raw OS capabilities into **ready-to-use .NET & IIS containers** still required solving offline CBS/DISM package dependencies, installerless ANCM V2 registration, and `.NET Framework 4.8.1` (`x86` & `x64`) CLR transplantation. **This repository is the culmination of that request — ready for any .NET developer to pull and run.**

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
| ❌ **No 32-bit (`x86` / WoW64) subsystem** (`SysWOW64` is missing, so 32-bit processes immediately fail to launch) | ✅ Pulls the exact build-matched `Microsoft-OneCore-WOW64-Package` + language CABs from Windows Update (`10.0.26100.32522` + `10.0.26100.1`) and installs `Microsoft.NanoServer.Datacenter.WOWSupport` offline via `DISM`. |
| ❌ **No IIS 10 included** | ✅ Pulls and installs `Microsoft-NanoServer-IIS-Package` offline via `DISM`, configuring `w3svc` and `DefaultAppPool`. |
| ❌ **No `msiexec` or `netsh` to install ASP.NET Core Hosting Bundle (ANCM V2)** | ✅ Extracts `aspnetcoremodule_x64_en_v2.msi` in a builder stage and uses a standalone C# tool (`RegisterAncm.exe`) to register `aspnetcorev2.dll` and inject the `system.webServer/aspNetCore` schema into `applicationHost.config`. |
| ❌ **No Full .NET Framework 4.8.1 (`net481`) or `System.Web`** | ✅ Transplants `C:\Windows\Microsoft.NET` (`Framework` + `Framework64`), native CLR CRT forwarders (`mscoree.dll`, `ucrtbase_clr0400.dll`, `vcruntime140_clr0400.dll`, `vcruntime140_1_clr0400.dll`), 32-bit/64-bit `inetsrv`, and NDP/ASP.NET registry hives from Server Core into Nano Server. |
| ❌ **No Single-Threaded Apartment (`STA` / `COINIT_APARTMENTTHREADED`) or Win32 message queue** (`win32u.dll` is omitted, causing `CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)` to return `0x80004001 E_NOTIMPL`) | ✅ Bakes a lightweight native user-mode STA & Win32 message-queue shim (`nanoserver_sta_shim.dll`, 32-bit & 64-bit) into `System32` and `SysWOW64` via `user32.dll` / `ole32.dll` forwarders so `ApartmentState.STA`, `OleMainThreadWndClass`, and Win32 thread message pumping work out of the box (toggleable via `NANOSERVER_DISABLE_STA_SHIM=1`). |

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
   - Contains the full C++ ATL test COM server source code (`Tests\src\NativeCOM\`), native lifetime probe C++ source (`Tests\src\NativeLifetimeProbe\`), prebuilt `NativeCOM.dll`, `NativeCOM.manifest`, `Interop.NativeCOM.dll`, and `NativeLifetimeProbe.dll` (`Tests\native\`), plus the complete `AdaskoTheBeAsT.Interop.COM` library and unit test suite (`Tests\src\AdaskoTheBeAsT.Interop.COM\` and `Tests\AdaskoTheBeAsT.Interop.COM.Test\`).
   - Builds and executes all unit tests inside `FROM adaskothebeast/nanoserver:<flavour>` across all 4 STA shim toggle modes (`Default`, `OS-Level STA Shim Only`, `Library In-Process STA Shim Only`, and `Both Disabled + Runtime Toggle`).

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

Each `push.ps1` tags and pushes a floating flavour tag plus exact, minor, and major version tags with the container revision suffix (`-v1.1.0`, `-v1.1`, `-v1`).

To bump the 3-tier version tags (`x.x.x`, `x.x`, and `x`) across all 6 subfolder `push.ps1` files at once, run **`set-version.ps1`**:

```powershell
# Updates all 6 push.ps1 files (both tag and push lines) to -v1.1.0, -v1.1, and -v1
.\set-version.ps1 -Version 1.1.0

# Optional: also bump the .NET 10 runtime version in Dockerfiles & push.ps1 files
.\set-version.ps1 -Version 1.1.0 -DotNetVersion 10.0.14
```

- **ASP.NET Core 10 (`x86` Kestrel):**
  - `adaskothebeast/nanoserver:aspnet-x86`
  - `adaskothebeast/nanoserver:aspnet-x86-10.0.14-ltsc2025-v1.1.0`
  - `adaskothebeast/nanoserver:aspnet-x86-10.0-ltsc2025-v1.1`
  - `adaskothebeast/nanoserver:aspnet-x86-10-ltsc2025-v1`
- **ASP.NET Core 10 (`x64` Kestrel):**
  - `adaskothebeast/nanoserver:aspnet-x64`
  - `adaskothebeast/nanoserver:aspnet-x64-10.0.14-ltsc2025-v1.1.0`
  - `adaskothebeast/nanoserver:aspnet-x64-10.0-ltsc2025-v1.1`
  - `adaskothebeast/nanoserver:aspnet-x64-10-ltsc2025-v1`
- **IIS 10 + ASP.NET Core 10 (`x86` OutOfProcess):**
  - `adaskothebeast/nanoserver:iis-aspnet-x86`
  - `adaskothebeast/nanoserver:iis-aspnet-x86-10.0.14-ltsc2025-v1.1.0`
  - `adaskothebeast/nanoserver:iis-aspnet-x86-10.0-ltsc2025-v1.1`
  - `adaskothebeast/nanoserver:iis-aspnet-x86-10-ltsc2025-v1`
- **IIS 10 + ASP.NET Core 10 (`x64` InProcess / OutOfProcess):**
  - `adaskothebeast/nanoserver:iis-aspnet-x64`
  - `adaskothebeast/nanoserver:iis-aspnet-x64-10.0.14-ltsc2025-v1.1.0`
  - `adaskothebeast/nanoserver:iis-aspnet-x64-10.0-ltsc2025-v1.1`
  - `adaskothebeast/nanoserver:iis-aspnet-x64-10-ltsc2025-v1`
- **IIS 10 + .NET Framework 4.8.1 (`x86`):**
  - `adaskothebeast/nanoserver:iis-netfx-x86`
  - `adaskothebeast/nanoserver:iis-netfx-x86-4.8.1-ltsc2025-v1.1.0`
  - `adaskothebeast/nanoserver:iis-netfx-x86-4.8-ltsc2025-v1.1`
  - `adaskothebeast/nanoserver:iis-netfx-x86-4-ltsc2025-v1`
- **IIS 10 + .NET Framework 4.8.1 (`x64`):**
  - `adaskothebeast/nanoserver:iis-netfx-x64`
  - `adaskothebeast/nanoserver:iis-netfx-x64-4.8.1-ltsc2025-v1.1.0`
  - `adaskothebeast/nanoserver:iis-netfx-x64-4.8-ltsc2025-v1.1`
  - `adaskothebeast/nanoserver:iis-netfx-x64-4-ltsc2025-v1`

---

## 🔬 Deep Dive: [`AdaskoTheBeAsT.Interop.Unmanaged`](https://github.com/AdaskoTheBeAsT/AdaskoTheBeAsT.Interop.Unmanaged) & [`AdaskoTheBeAsT.Interop.COM`](https://github.com/AdaskoTheBeAsT/AdaskoTheBeAsT.Interop.COM) on Nano Server 2025

| Interop Mechanism | Status on Nano Server 2025 | Technical Details |
| :--- | :--- | :--- |
| ✅ **Plain Native DLLs via [`AdaskoTheBeAsT.Interop.Unmanaged`](https://github.com/AdaskoTheBeAsT/AdaskoTheBeAsT.Interop.Unmanaged)** | **Works 100% Out of the Box (`x86` & `x64`)** | Standard C/C++/Delphi/PowerBasic/Fortran DLL loading (`LoadLibrary` / `GetProcAddress` / `FreeLibrary`) works natively in both 32-bit (`SysWOW64`) and 64-bit (`System32`). *(Tip: If your native DLL links against the Visual C++ Redistributable like `vcruntime140.dll` / `msvcp140.dll`, copy those CRT DLLs next to your native DLL.)* |
| ⚠️ **SxS Registration-Free COM via `CreateActCtxW` (`*.manifest`)** | **Blocked by `apisetschema.dll` (even if `sxs.dll` is copied)** | `KernelBase!CreateActCtxW` checks `IsCreateActCtxWWorkerPresent()` (`ext-ms-win-kernel32-sidebyside-l1-1-0`) in the kernel-mapped `apisetschema.dll` *before* calling `sxs.dll`, returning `ERROR_INVALID_FUNCTION (1)`. Copying Server Core's `apisetschema.dll` or `sxssrv.dll` breaks container silo boot (`0xc0370111` at `hcs::System::Start`) because Server Core's schema redirects console/windowing API sets away from Nano Server's headless `coniosrv.dll`. |
| ✅ **Registration-Free COM with `.manifest` via `DllGetClassObject` + `CoRegisterClassObject` Fallback** | **Works 100% (`x86` & `x64` on STA & MTA)** | Verified with `NativeCOM.dll` + `NativeCOM.manifest`: when `CreateActCtxW` returns `ERROR_INVALID_FUNCTION (1)`, parsing `<comClass clsid="{...}"/>` from the `.manifest` file, calling `DllGetClassObject` on the COM DLL, and registering the `IClassFactory` in-memory via `combase!CoRegisterClassObject(..., CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE, out cookie)` allows `new StringConcatenatorClass()` (`CoCreateInstance`) to succeed with `S_OK` on both `x86` and `x64` Nano Server without `regsvr32` or OS modifications! |
| ✅ **Single-Threaded Apartment (`STA` / `COINIT_APARTMENTTHREADED`) & Win32 Message Pumping** | **Works 100% (`x86` & `x64` via OS-level & in-process STA shims)** | Out of the box, unpatched Nano Server returns `0x80004001` (`E_NOTIMPL`) from `CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)` because `combase.dll` checks `ext-ms-win-rtcore-ntuser-window-ext-l1-1-0` (`win32u.dll`) to create the hidden `OleMainThreadWndClass` STA window. Both these container images (via the native `nanoserver_sta_shim.dll` in `System32` / `SysWOW64`) and `AdaskoTheBeAsT.Interop.COM` (via its in-process `NanoServerStaShim`) redirect the `combase.dll` / `ole32.dll` / `user32.dll` window & message-queue IAT slots to an in-memory user-mode Win32 window and message-queue implementation (`RegisterClassExW`, `CreateWindowExW`, `PeekMessageW`, `PostMessageW`, `SendMessageW`, `MsgWaitForMultipleObjectsEx`, timer & property tables), enabling full `ApartmentState.STA` and `pumpPendingMessages: true` on `x86` and `x64`! |

### 🎛️ Toggling the OS-Level and Library-Level Shims (for Testing & Diagnostics)

Both the container-level native STA shim (`nanoserver_sta_shim.dll`) and the `AdaskoTheBeAsT.Interop.COM` library-level shims can be independently toggled off via environment variables or at runtime via `NanoServerOptions`:

| Environment Variable / API | Target | Effect when Set to `1` / `true` |
| :--- | :--- | :--- |
| `NANOSERVER_DISABLE_STA_SHIM=1` | **OS-Level Native Shim** (`nanoserver_sta_shim.dll` in `System32` & `SysWOW64`) | Disables the container's native IAT hooks on `combase.dll`, `ole32.dll`, and `user32.dll` (and exports `NanoServerStaShim_RestoreOriginal()` / `NanoServerStaShim_Refresh()` so managed code can toggle it dynamically). |
| `ADASKO_NANOSERVER_DISABLE_STA_SHIM=1` / `NanoServerOptions.EnableStaShim = false` | **Library-Level In-Process STA Shim** (`AdaskoTheBeAsT.Interop.COM`) | Disables `AdaskoTheBeAsT.Interop.COM`'s built-in managed STA & Win32 message-queue shim (`NanoServerStaShim`). When both `NANOSERVER_DISABLE_STA_SHIM=1` and `ADASKO_NANOSERVER_DISABLE_STA_SHIM=1` are set, unshimmed Nano Server behavior is restored (`CoInitializeEx(..., COINIT_APARTMENTTHREADED)` returns `E_NOTIMPL`). |
| `ADASKO_NANOSERVER_DISABLE_SXS_FALLBACK=1` / `NanoServerOptions.EnableSxsFallback = false` | **Library-Level SxS Manifest Fallback** (`AdaskoTheBeAsT.Interop.COM`) | Disables the automatic `DllGetClassObject` + `CoRegisterClassObject` manifest fallback when `CreateActCtxW` fails on Nano Server. |

### ⚖️ Which Method Is Better: OS-Level Container Shim (`nanoserver_sta_shim.dll`) vs. `AdaskoTheBeAsT.Interop.COM` In-Process Shim?

Both mechanisms pass 100% of the COM, STA, and Win32 message-pumping test suite (`174/174` on .NET 10 and `175/175` on .NET Framework 4.8.1), but they operate at different layers and complement each other:

| Criterion | OS-Level Native Shim (`nanoserver_sta_shim.dll` in Container) | Library-Level In-Process Shim (`AdaskoTheBeAsT.Interop.COM`) |
| :--- | :--- | :--- |
| **Initialization Timing** | ✅ **Best:** Loaded by the Windows PE loader during `DLL_PROCESS_ATTACH` of `user32.dll` / `ole32.dll` **before the .NET CLR or `Main()` starts**. Even `[STAThread]` on `Main()` and early native `CoInitialize` calls work immediately. | ⚠️ Loaded when `AdaskoTheBeAsT.Interop.COM.dll` is first loaded into the process (`[ModuleInitializer]` on .NET 8/9/10, or first call into `ComExecutor` / `NanoServerOptions` on .NET Framework). |
| **Runtime Call Overhead** | ✅ **Best:** Pure native C++ (`nanoserver_sta_shim.dll` ➡️ `combase.dll` / `ole32.dll`). Zero managed/unmanaged reverse-P/Invoke transitions or GC pinning overhead during internal COM apartment window/message calls. | ⚡ Managed reverse-P/Invoke delegates (`UnmanagedCallersOnly` / `Marshal.GetFunctionPointerForDelegate`). Fast enough for typical COM activation and message pumping, but crosses the managed/unmanaged boundary. |
| **Scope of Protection** | ✅ **Process-wide:** Works for **any** code in the container (including third-party COM wrappers, `Activator.CreateInstance`, or native C++ code that does not reference `AdaskoTheBeAsT.Interop.COM`). | 🎯 Scoped to processes that load `AdaskoTheBeAsT.Interop.COM`. |
| **Registration-Free COM (`*.manifest` SxS)** | ❌ Does **not** parse `.manifest` files (`CreateActCtxW` is blocked by Nano Server's kernel `apisetschema.dll`). Without `AdaskoTheBeAsT.Interop.COM`, COM DLLs must be registered via `regsvr32`. | ✅ **Best:** Automatically parses `<comClass clsid="{...}"/>` from `.manifest` XML and registers `IClassFactory` in-memory via `DllGetClassObject` + `CoRegisterClassObject` without `regsvr32`. |
| **Works on Stock Microsoft Nano Server Images?** | ❌ Requires `install_sta_shim.exe` to have been run when building the container image (pre-installed in all `adaskothebeast/nanoserver` images). | ✅ **Best:** Zero Dockerfile/OS changes required — works on stock `mcr.microsoft.com/windows/nanoserver:ltsc2025` and `mcr.microsoft.com/dotnet/aspnet:10.0-nanoserver-ltsc2025` purely by referencing the NuGet package. |

#### 🏆 Recommendation

1. **For STA & Win32 Message Pumping (`COINIT_APARTMENTTHREADED`):** **The OS-level native shim (`nanoserver_sta_shim.dll`) baked into these `adaskothebeast/nanoserver` images is better.** Because it is linked directly into the container's `user32.dll` and `ole32.dll` forwarders, it initializes before the CLR starts, supports `[STAThread]` on `Main()`, avoids managed reverse-P/Invoke overhead, and never has to worry about CLR module-initializer lock ordering.
2. **For Registration-Free COM (`*.manifest` without `regsvr32`):** **`AdaskoTheBeAsT.Interop.COM` is essential**, because it provides the manifest-driven `DllGetClassObject` + `CoRegisterClassObject` fallback that bypasses Nano Server's missing Side-by-Side (`CreateActCtxW`) subsystem.
3. **Best Production Setup (Default):** **Use `AdaskoTheBeAsT.Interop.COM` on top of `adaskothebeast/nanoserver`.** When both are present, `AdaskoTheBeAsT.Interop.COM` automatically detects `nanoserver_sta_shim.dll` in the process and delegates STA/window management to the faster native OS-level shim while handling registration-free `.manifest` activation and lifetime management in managed code. And if your code is ever deployed to a stock Microsoft Nano Server container without `nanoserver_sta_shim.dll`, `AdaskoTheBeAsT.Interop.COM`'s built-in STA shim seamlessly takes over as a fallback.

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

Crucially, **step 1 (`CoRegisterClassObject`) runs *before* step 2 (SxS) and step 3 (Registry), and `CoRegisterClassObject` is 100% present and functional in Nano Server's `combase.dll`!**

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

Why does `CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)` fail with `0x80004001` (`E_NOTIMPL`) on Nano Server?

Because a Single-Threaded Apartment (STA) is, by definition in the COM specification, **a thread whose incoming cross-apartment calls are serialized through a Win32 window message queue**. When the first STA thread initializes in `combase.dll` (`InitMainThreadWnd` / `RegisterOleWndClass`), `combase.dll` calls:
- `IsRegisterClassWPresent()` (checking `ext-ms-win-rtcore-ntuser-window-ext-l1-1-0`)
- `RegisterClassExW(&wndClass)` for `"OleMainThreadWndClass"`
- `CreateWindowExW(0, L"OleMainThreadWndClass", L"OleMainThreadWndName", WS_DISABLED, ..., HWND_MESSAGE, ...)`

When another thread makes a COM call into that STA apartment, `combase.dll` posts or sends a custom Win32 message to that hidden `HWND_MESSAGE` window, and the STA thread's message loop (`PeekMessageW` / `GetMessageW` / `DispatchMessageW` / `MsgWaitForMultipleObjectsEx`) dispatches the message into `combase!ThreadWndProc`, which executes the target COM vtable method on the owning STA thread.

Notice the profound architectural insight here:
> **`combase.dll` does NOT need pixels, a monitor, GDI, DirectX, or a kernel-mode display driver (`win32k.sys`).** It only needs:
> 1. A handle table mapping synthetic `HWND` values to an owning `ThreadId`, class name, `WNDPROC` function pointer, `GWLP_USERDATA`, and window property bag (`SetPropW` / `GetPropW` / `RemovePropW`).
> 2. A per-thread FIFO message queue (`MSG`) backed by a real Win32 manual-reset `EVENT` handle (`CreateEventW`) so `PostMessageW` / `PostThreadMessageW` can wake a sleeping `MsgWaitForMultipleObjectsEx` or `GetMessageW` call.
> 3. Synchronous same-thread dispatch (`SendMessageW` / `DispatchMessageW`) that directly invokes the registered `WNDPROC` callback (`lpfnWndProc(hwnd, msg, wParam, lParam)`).

We built this exact headless Win32 window and message-queue engine in **both** pure C++ (`nanoserver_sta_shim.dll`) and C# (`NanoServerStaShim` + `NanoServerWindowManager`).

#### How the OS-Level Shim (`install_sta_shim.exe` + `nanoserver_sta_shim.dll`) Hooks Every Process Without Touching the Kernel

Getting `combase.dll` to use our user-mode window engine automatically in *every* container process (before `.NET` or `Main()` even starts) required solving two deep Windows loader mechanisms: **`\KnownDlls`** and **Delay-Loaded / API-Set Import Address Tables (IATs)**:

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  Step 1 (Container Build Time — install_sta_shim.exe)                        │
│  • Safety Check: Aborts immediately if C:\Windows\System32\win32u.dll exists │
│  • Removes "user32" and "ole32" from HKLM\...\Session Manager\KnownDlls      │
│    so ntdll loads System32\user32.dll & ole32.dll from disk, not kernel RAM  │
│  • Rewrites the PE Import Directory (IMAGE_IMPORT_DESCRIPTOR) of             │
│    System32\forwarders\user32.dll & ole32.dll (and SysWOW64\forwarders\*)    │
│    to add a static import on nanoserver_sta_shim.dll!NanoServerStaShim_Init  │
└──────────────────────────────────────┬───────────────────────────────────────┘
                                       │
                                       ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│  Step 2 (Process Startup — ntdll.dll PE Loader)                              │
│  • Process loads user32.dll or ole32.dll                                     │
│  • PE loader automatically loads nanoserver_sta_shim.dll and runs DllMain    │
└──────────────────────────────────────┬───────────────────────────────────────┘
                                       │
                                       ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│  Step 3 (IAT Patching in DllMain / NanoServerStaShim_Refresh)                │
│  • Walks IMAGE_IMPORT_DESCRIPTOR & IMAGE_DELAYLOAD_DESCRIPTOR of loaded      │
│    modules (combase.dll, ole32.dll, user32.dll)                              │
│  • Redirects IsRegisterClassWPresent() ➡️ Shim_AlwaysTrue() (returns 1)      │
│  • Redirects RegisterClassExW, CreateWindowExW, DestroyWindow,               │
│    PeekMessageW, GetMessageW, TranslateMessage, DispatchMessageW,            │
│    PostMessageW, PostThreadMessageW, SendMessageW, DefWindowProcW,           │
│    MsgWaitForMultipleObjectsEx, SetTimer, KillTimer, SetPropW, GetPropW,     │
│    RemovePropW, GetWindowLongPtrW, SetWindowLongPtrW, GetWindowThread-       │
│    ProcessId, IsWindow ➡️ User-Mode Ring-3 Window & Message-Queue Engine     │
└──────────────────────────────────────────────────────────────────────────────┘
```

Why did `install_sta_shim.exe` have to remove `user32` and `ole32` from `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\KnownDlls`?
Because at boot time, the Session Manager (`smss.exe`) pre-maps every DLL listed in `KnownDlls` into the kernel Object Manager directory `\KnownDlls` (64-bit) and `\KnownDlls32` (32-bit). When a process loads a DLL listed in `\KnownDlls`, `ntdll.dll` maps the cached section object and **ignores** changes on disk if the section is already pre-configured, or fails if a `KnownDll` depends on a non-`KnownDll`. Removing `user32` and `ole32` from the container's `KnownDlls` registry key forces `ntdll.dll` to load our patched `user32.dll` and `ole32.dll` cleanly from `C:\Windows\System32` (and `C:\Windows\SysWOW64`), pulling `nanoserver_sta_shim.dll` into every 32-bit and 64-bit COM process automatically!

---

## 🛡️ Security & Parity Audit: Is This Really as Good and Secure as Microsoft's Native Solution?

As a seasoned architect, you should always ask two hard questions before putting low-level interop or OS shims into production:
1. **Is it secure?**
2. **Is the replaced functionality truly on par with Microsoft's native implementation, and where are the exact boundaries?**

### 1. Security Architecture: Why User-Mode STA Shimming Is Actually *Safer* Than Server Core

| Security Dimension | Full Windows Server Core (`win32k.sys` + `sxssrv.dll`) | Nano Server + `nanoserver_sta_shim.dll` / `AdaskoTheBeAsT.Interop.COM` |
| :--- | :--- | :--- |
| **Kernel Ring-0 Attack Surface (`win32k.sys`)** | ⚠️ **High historical CVE surface:** `win32u.dll` issues hundreds of system calls into Ring-0 `win32k.sys` / `win32kfull.sys`. Over the past 25 years, `win32k.sys` has been the **#1 source of Windows Kernel Elevation-of-Privilege (EoP) vulnerabilities** because complex window/menu/message state is parsed inside kernel mode. | ✅ **Zero Ring-0 GUI surface:** Everything in `nanoserver_sta_shim.dll` and `NanoServerStaShim` runs in **unprivileged Ring-3 user mode** inside the calling process's private virtual address space. A bug in a COM component cannot exploit a `win32k.sys` kernel syscall because `win32k.sys` is never called. |
| **Cross-Process Isolation (UIPI / Shatter Attacks)** | ⚠️ Shared desktop heaps and cross-process window message queues require User Interface Privilege Isolation (UIPI) in the kernel to prevent low-integrity processes from sending malicious messages ("Shatter attacks") to higher-privilege windows. | ✅ **Strict Process Isolation:** The shim's handle table (`g_windows`) and thread message queues (`g_queues`) live in process-local memory (`SRWLOCK` / `ConcurrentDictionary`). Other processes cannot enumerate, message, or hook your process's `HWND`s at all. |
| **Memory Protection (`W^X` Compliance)** | ✅ Kernel/loader enforced. | ✅ **No executable heap (`PAGE_EXECUTE_READWRITE`) is ever allocated.** IAT patching temporarily sets `PAGE_READWRITE` on a single 4-byte/8-byte data pointer slot in `.rdata`/`.idata`, writes the function pointer atomically, and immediately restores the original `PAGE_READONLY` protection. |
| **XML Manifest Parsing Security** | ✅ Parsed by `sxssrv.dll` in CSRSS (system-privileged process). | ✅ Parsed in unprivileged user mode via `XmlReader` with `DtdProcessing.Prohibit` and `XmlResolver = null`—completely immune to XXE (XML External Entity), SSRF, and Billion Laughs entity-expansion attacks. |
| **Host Machine Safety Guard** | N/A | ✅ **Hard-coded fail-safe:** The very first line of `wmain` in `install_sta_shim.cpp` checks `GetFileAttributesW(L"C:\\Windows\\System32\\win32u.dll") != INVALID_FILE_ATTRIBUTES`. If `win32u.dll` exists (meaning it is running on a Windows 11/10 developer workstation or Windows Server Core host), it immediately aborts with exit code `1` before touching any file or registry key. |

### 2. Engineering Parity: How Does Our Implementation Compare to Microsoft's Native Subsystems?

Here is the honest, no-marketing comparison of where our SxS and STA replacements are **100% equivalent (or faster)** than Microsoft's native implementation, and the **exact architectural boundaries** you should know:

| Subsystem / Feature | Parity with Native Windows (`sxs.dll` / `win32u.dll`) | Why & Architectural Trade-Offs to Know |
| :--- | :--- | :--- |
| **In-Process COM Object Creation & Vtable Calls (`CLSCTX_INPROC_SERVER`)** | **100% Identical (`S_OK`)** | Neither SxS nor our fallback changes how COM objects execute. Once `DllGetClassObject` returns the native `IClassFactory` and `combase.dll` calls `IClassFactory::CreateInstance`, the returned `IUnknown*` / `IDispatch*` pointer, vtable layout, RCW marshaling, and native machine code execution are **100% Microsoft's native `combase.dll` and your native C++/Delphi/VB6 DLL**. |
| **STA Apartment Serialization & Message Pumping (`COINIT_APARTMENTTHREADED`)** | **100% Compatible & Faster in Ring 3** | For `OleMainThreadWndClass`, `SendMessageW`, `PostMessageW`, `PeekMessageW`, `GetMessageW`, `DispatchMessageW`, `MsgWaitForMultipleObjectsEx`, `SetPropW`/`GetPropW`, and `SetTimer`/`KillTimer`, running in Ring-3 user mode avoids the Ring-3 $\leftrightarrow$ Ring-0 kernel syscall transition overhead on every message pump cycle. |
| **Scope of Registration-Free Manifest Activation** | **Process-Wide (`CoRegisterClassObject`) vs. Per-Thread (`ActivateActCtx`)** | Native Windows SxS pushes an activation context onto the current thread's `TEB->ActivationContextStackPointer`. Our Nano Server fallback registers the manifest's `IClassFactory` in `combase.dll`'s process-wide class table for the duration of the `ActivationContext` scope. For 99.9% of services, this is identical (or even convenient if worker threads are spawned), unless a single process simultaneously loads *two different versions of the exact same `CLSID`* on two different threads at the exact same millisecond. |
| **Manifest Elements Supported** | **`<file>` & `<comClass clsid="{...}"/>` (In-Proc COM)** | Our manifest parser specifically targets `<file>` and `<comClass>` for in-process COM activation (`DllGetClassObject`). It does not emulate kernel Win32 assembly binding redirects for non-COM DLLs or `<comInterfaceExternalProxyStub>` custom proxy-stub DLL registration (note: standard `oleaut32.dll` / `IDispatch` / TypeLib automation marshaling still works natively). |
| **Cross-Thread Synchronous `SendMessageW` & Visible GUI Windows** | **Headless / Same-Thread `SendMessageW` + Cross-Thread `PostMessageW`** | Our user-mode Win32 engine is purposefully built for **headless server workloads** (`HWND_MESSAGE`, STA apartment windows, thread message queues, timers, and window properties). Cross-thread `PostMessageW` / `PostThreadMessageW` + `MsgWaitForMultipleObjectsEx` works across threads via OS event handles, and same-thread `SendMessageW` / `DispatchMessageW` invokes `WNDPROC` directly. It does **not** render visible pixels (GDI/DirectX) or perform cross-process `HWND_BROADCAST`. |

---


