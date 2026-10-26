# 🚀 Windows Nano Server 2025 — IIS 10, ASP.NET Core 10 & .NET Framework 4.8.1 (`x86` & `x64`)

**Run the "impossible" .NET workloads on Windows Nano Server 2025 (`ltsc2025`).**

This repository provides production-ready, reusable **Nano Server 2025 base images** (and sample `HealthApi` apps) published to a single unified Docker Hub repository (**`adaskothebeast/nanoserver`**) for the exact .NET scenarios where Microsoft's official images normally force you onto massive **5–8 GB Windows Server Core** containers:

- ⚡ **32-bit (`x86`) ASP.NET Core 10** — Self-hosted **Kestrel** & **IIS 10 (`OutOfProcess`)**
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

---

## 📦 All 5 Flavours at a Glance

*(Note: 64-bit self-hosted Kestrel without IIS is already covered by Microsoft's official `mcr.microsoft.com/dotnet/aspnet:10.0-nanoserver-ltsc2025` image.)*

| Folder | Docker Hub Tag (`adaskothebeast/nanoserver`) | Runtime | Web Server & Hosting | Bitness | Image Size | Port |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 📁 [`nanoserver-aspnet-x86`](./nanoserver-aspnet-x86) | `:aspnet-x86` | **ASP.NET Core 10.0** (`win-x86`) | **Kestrel** (Self-hosted) | 32-bit (`x86`) | **1.41 GB** | `8080` |
| 📁 [`nanoserver-iis-aspnet-x86`](./nanoserver-iis-aspnet-x86) | `:iis-aspnet-x86` | **ASP.NET Core 10.0** (`win-x86`) | **IIS 10** + ANCM V2 (`OutOfProcess`) | 64-bit IIS ➡️ 32-bit `dotnet.exe` | **2.48 GB** | `80` |
| 📁 [`nanoserver-iis-aspnet-x64`](./nanoserver-iis-aspnet-x64) | `:iis-aspnet-x64` | **ASP.NET Core 10.0** (`win-x64`) | **IIS 10** + ANCM V2 (`InProcess` / `OutOfProcess`) | 64-bit (`x64`) | **2.29 GB** | `80` |
| 📁 [`nanoserver-iis-netfx-x86`](./nanoserver-iis-netfx-x86) | `:iis-netfx-x86` | **.NET Framework 4.8.1** (`net481`) | **IIS 10** (`System.Web` Integrated) | 32-bit (`x86` `w3wp.exe`) | **2.03 GB** | `80` |
| 📁 [`nanoserver-iis-netfx-x64`](./nanoserver-iis-netfx-x64) | `:iis-netfx-x64` | **.NET Framework 4.8.1** (`net481`) | **IIS 10** (`System.Web` Integrated) | 64-bit (`x64` `w3wp.exe`) | **1.93 GB** | `80` |

---

## 🏗️ Two-Part Architecture: Base Host vs. Sample App

Every folder is cleanly separated into two parts:

1. 🧱 **Part 1 — Reusable Base Host Container (`<folder>\Dockerfile`)**
   - Built once and pushed to Docker Hub (`adaskothebeast/nanoserver:<flavour>`).
   - Uses `.dockerignore` to ignore `HealthApi\` so application code changes never trigger a base image rebuild.
2. 🚀 **Part 2 — Sample `HealthApi` Container (`<folder>\HealthApi\Dockerfile`)**
   - Compiles `HealthApi.csproj` using `mcr.microsoft.com/dotnet/sdk:10.0-windowsservercore-ltsc2025` as a multi-stage build step.
   - Layers the published output onto `FROM adaskothebeast/nanoserver:<flavour>`.
   - Serves `GET /health` ➡️ `200 OK` `{"status":"Healthy"}`.

---

## 🛠️ Build, Run & Push Scripts

Each flavour uses the same simple `.bat` scripts:

### 1️⃣ Base Host Container (run from `<folder>\`)

- 🔨 **`go.bat`** — Build the reusable base host image:

  ```bat
  docker build -t adaskothebeast/nanoserver:<flavour> .
  ```

- ▶️ **`run.bat`** — Run the base host container interactively on port `8080`:

  ```bat
  docker run --rm -it -p 8080:80 adaskothebeast/nanoserver:<flavour>
  ```

  *(Uses `-p 8080:8080` for `nanoserver-aspnet-x86`.)*
- ☁️ **`push.bat`** — Tag and push all version tags to Docker Hub (`adaskothebeast/nanoserver`).

### 2️⃣ Sample `HealthApi` Container (run from `<folder>\HealthApi\`)

- 🔨 **`HealthApi\go.bat`** — Build the sample `HealthApi` container on top of the base image:

  ```bat
  docker build -t adaskothebeast/nanoserver:<flavour>-sample .
  ```

- ▶️ **`HealthApi\run.bat`** — Run the sample `HealthApi` container on port `8080`:

  ```bat
  docker run --rm -it -p 8080:80 adaskothebeast/nanoserver:<flavour>-sample
  ```

- ✅ **Verify the health check endpoint:**

  ```powershell
  curl.exe -i --fail http://localhost:8080/health
  # HTTP/1.1 200 OK
  # {"status":"Healthy"}
  ```

---

## 🐳 Docker Hub Tags (`adaskothebeast/nanoserver`)

Each `push.bat` tags and pushes a floating flavour tag plus exact, minor, and major version tags with the container revision suffix (`-v1.0.0`, `-v1.0`, `-v1`):

- **ASP.NET Core 10 (`x86` Kestrel):**
  - `adaskothebeast/nanoserver:aspnet-x86`
  - `adaskothebeast/nanoserver:aspnet-x86-10.0.13-ltsc2025-v1.0.0`
  - `adaskothebeast/nanoserver:aspnet-x86-10.0-ltsc2025-v1.0`
  - `adaskothebeast/nanoserver:aspnet-x86-10-ltsc2025-v1`
- **IIS 10 + ASP.NET Core 10 (`x86` OutOfProcess):**
  - `adaskothebeast/nanoserver:iis-aspnet-x86`
  - `adaskothebeast/nanoserver:iis-aspnet-x86-10.0.13-ltsc2025-v1.0.0`
  - `adaskothebeast/nanoserver:iis-aspnet-x86-10.0-ltsc2025-v1.0`
  - `adaskothebeast/nanoserver:iis-aspnet-x86-10-ltsc2025-v1`
- **IIS 10 + ASP.NET Core 10 (`x64` InProcess / OutOfProcess):**
  - `adaskothebeast/nanoserver:iis-aspnet-x64`
  - `adaskothebeast/nanoserver:iis-aspnet-x64-10.0.13-ltsc2025-v1.0.0`
  - `adaskothebeast/nanoserver:iis-aspnet-x64-10.0-ltsc2025-v1.0`
  - `adaskothebeast/nanoserver:iis-aspnet-x64-10-ltsc2025-v1`
- **IIS 10 + .NET Framework 4.8.1 (`x86`):**
  - `adaskothebeast/nanoserver:iis-netfx-x86`
  - `adaskothebeast/nanoserver:iis-netfx-x86-4.8.1-ltsc2025-v1.0.0`
  - `adaskothebeast/nanoserver:iis-netfx-x86-4.8-ltsc2025-v1.0`
  - `adaskothebeast/nanoserver:iis-netfx-x86-4-ltsc2025-v1`
- **IIS 10 + .NET Framework 4.8.1 (`x64`):**
  - `adaskothebeast/nanoserver:iis-netfx-x64`
  - `adaskothebeast/nanoserver:iis-netfx-x64-4.8.1-ltsc2025-v1.0.0`
  - `adaskothebeast/nanoserver:iis-netfx-x64-4.8-ltsc2025-v1.0`
  - `adaskothebeast/nanoserver:iis-netfx-x64-4-ltsc2025-v1`

---

## 🔬 Deep Dive: [`AdaskoTheBeAsT.Interop.Unmanaged`](https://github.com/AdaskoTheBeAsT/AdaskoTheBeAsT.Interop.Unmanaged) & [`AdaskoTheBeAsT.Interop.COM`](https://github.com/AdaskoTheBeAsT/AdaskoTheBeAsT.Interop.COM) on Nano Server 2025

| Interop Mechanism | Status on Nano Server 2025 | Technical Details |
| :--- | :--- | :--- |
| ✅ **Plain Native DLLs via [`AdaskoTheBeAsT.Interop.Unmanaged`](https://github.com/AdaskoTheBeAsT/AdaskoTheBeAsT.Interop.Unmanaged)** | **Works 100% Out of the Box (`x86` & `x64`)** | Standard C/C++/Delphi/PowerBasic/Fortran DLL loading (`LoadLibrary` / `GetProcAddress` / `FreeLibrary`) works natively in both 32-bit (`SysWOW64`) and 64-bit (`System32`). *(Tip: If your native DLL links against the Visual C++ Redistributable like `vcruntime140.dll` / `msvcp140.dll`, copy those CRT DLLs next to your native DLL.)* |
| ⚠️ **SxS Registration-Free COM via `CreateActCtxW` (`*.manifest`)** | **Blocked by `apisetschema.dll` (even if `sxs.dll` is copied)** | `KernelBase!CreateActCtxW` checks `IsCreateActCtxWWorkerPresent()` (`ext-ms-win-kernel32-sidebyside-l1-1-0`) in the kernel-mapped `apisetschema.dll` *before* calling `sxs.dll`, returning `ERROR_INVALID_FUNCTION (1)`. Copying Server Core's `apisetschema.dll` or `sxssrv.dll` breaks container silo boot (`0xc0370111` at `hcs::System::Start`) because Server Core's schema redirects console/windowing API sets away from Nano Server's headless `coniosrv.dll`. |
| ✅ **Registration-Free COM with `.manifest` via `DllGetClassObject` + `CoRegisterClassObject` Fallback** | **Works 100% (`x86` & `x64` on MTA)** | Verified with `NativeCOM.dll` + `NativeCOM.manifest`: when `CreateActCtxW` returns `ERROR_INVALID_FUNCTION (1)`, parsing `<comClass clsid="{...}"/>` from the `.manifest` file, calling `DllGetClassObject` on the COM DLL, and registering the `IClassFactory` in-memory via `combase!CoRegisterClassObject(..., CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE, out cookie)` allows `new StringConcatenatorClass()` (`CoCreateInstance`) to succeed with `S_OK` on both `x86` and `x64` Nano Server without `regsvr32` or OS modifications! |
| ❌ **Single-Threaded Apartment (`STA` / `COINIT_APARTMENTTHREADED`)** | **Blocked by `combase.dll`** | `Thread.SetApartmentState(ApartmentState.STA)` / `CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)` returns `0x80004001` (`E_NOTIMPL`) because STA requires a hidden Win32 message window (`win32u.dll`). Run COM calls on default **MTA** threads (`pumpPendingMessages: false`) on Nano Server. |
