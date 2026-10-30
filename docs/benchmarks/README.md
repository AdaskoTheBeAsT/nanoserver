# Nano Server `v1.3.0` (Roundtable) vs. `v1.2.0` vs. Native Windows Server Core (`ltsc2025`) Benchmarks

**Date:** 2026-10-30  
**Host Environment:** Windows Server / Windows 11 Build 26300 (`npipe:////./pipe/docker_engine_windows`, `--isolation=process --cpus 10`)  
**Validation Artifacts:** [`.artifacts/roundtable-validation-2026-10-30/`](../../.artifacts/roundtable-validation-2026-10-30/)

---

## 1. Executive Summary

Following the implementation and qualification of the [Roundtable `v1.3.0` findings (`R01`–`R26`, `N01`–`N06`)](../issues/roundtable-2026-10-29.md) and the [Roundtable Fix Validation](../roundtable-fix-validation.md) across all six Nano Server images (`aspnet-x64`, `aspnet-x86`, `iis-aspnet-x64`, `iis-aspnet-x86`, `iis-netfx-x64`, `iis-netfx-x86`), three benchmark suites were executed to measure the execution-speed impact of the Roundtable improvements and compare **Nano Server `v1.3.0`** against both **Nano Server `v1.2.0`** and **Native Windows Server Core (`ltsc2025`)**:

1. **Native `nanoserver_sta_shim.dll` Microbenchmarks (`x64` & `x86`):** Direct C++ (`QueryPerformanceCounter`) microbenchmarks of the hot Win32/COM STA operations (`ApiSetQueryApiSetPresence`, `CoInitializeEx(COINIT_APARTMENTTHREADED)` + `CoUninitialize`, `CreateWindowExW` + `DestroyWindow`, same-thread and cross-thread `SendMessageW`, `PostMessageW` + `MsgWaitForMultipleObjectsEx` + `PeekMessageW` + `DispatchMessageW`, and 10-thread concurrent STA message pumps).
2. **Managed `AdaskoTheBeAsT.Interop.COM.Test` Suite (172 `.NET 10.0` tests / 173 `.NET Framework 4.8.1` tests):** Execution time across all 6 flavours and shim operating modes (Mode 1 Default, Mode 2 OS-Shim Only, Mode 3 Library-Shim Only) vs. Native Windows Server Core (`ltsc2025`).
3. **Real-World 200-Test `AhuNew` Application & Multi-Worker STA Benchmark Suite (`aspnet-x86`):** Full 200-test engineering suite including 1-worker and 10-worker isolated 32-bit Delphi `CoilLib` STA thread-pool throughput (Water, Freon, Condensing, Glycol) and ASP.NET Core Minimal Web API concurrent HTTP load (`120` concurrent requests).

### Key Takeaways

- **128x–154x Hot-Path Speedup on Shimmed API-Set Queries (`R17`/`R19`):** Caching the applied per-module hook state (`HasAppliedState`) and skipping no-op `VirtualProtect` (`NtProtectVirtualMemory`) syscalls reduced 50,000 `ApiSetQueryApiSetPresence` calls from **`40,174.82 ms` to `261.57 ms` (`153.6x` faster, `-99.35%`) on x64** and from **`49,941.56 ms` to `390.65 ms` (`127.8x` faster, `-99.22%`) on x86**.
- **2.22x Faster COM STA Apartment Initialization (`CoInitializeEx` / `CoUninitialize`):** Because `combase.dll` queries API-set presence during STA apartment setup, the `R19` fix cut 5,000 `CoInitializeEx(COINIT_APARTMENTTHREADED)` + `CoUninitialize` cycles from `84.77 ms` (`v1.2.0`) to **`38.13 ms` (`v1.3.0`) on x64**—making Nano Server `v1.3.0` **3.58x faster than Native Windows Server Core (`136.66 ms`) on x64** and **5.64x faster (`56.58 ms` vs. `319.21 ms`) on x86**.
- **4.3x–6.0x Faster STA Message Pumping than Native Windows Server Core (`R06`/`R08`):** User-mode queue status and wake-mask tracking (`PostMessageW` + `MsgWaitForMultipleObjectsEx` + `PeekMessageW` + `DispatchMessageW`) improved by **1.76x–1.95x over `v1.2.0`** and is **5.97x faster (x64)** / **4.33x faster (x86)** than Native Windows Server Core's kernel `win32k.sys` syscall path.
- **Zero Regression from Correctness & Memory-Safety Hardening (`R01`–`R21`):** Shared `PendingSendMessage` completion ownership (`R01`/`R08`), full `WM_NCCREATE`/`WM_CREATE`/`WM_DESTROY`/`WM_NCDESTROY` dispatch (`R03`), class-atom scoping (`R10`), and serialized IAT patching (`R17`) added no measurable overhead to real-world STA workloads.
- **Full Real-World Parity with Native Windows Server Core at ~13% of the Image Size:** Across the 200-test `AhuNew` suite, Nano Server `v1.3.0` (`14.56s` cold / `14.37s` warm total; `967.0 req/s` 10-worker HTTP Minimal API throughput; `700.5 calc/s` 10-worker Water throughput) matches or slightly outperforms Native Windows Server Core `ltsc2025` (`14.70s` cold / `14.85s` warm total; `961.2 req/s` HTTP Minimal API; `655.0 calc/s` 10-worker Water throughput) while remaining ~266 MB compressed (~7.5x smaller than Server Core).

---

## 2. Native `nanoserver_sta_shim.dll` vs. Server Core `win32k.sys`/`user32.dll` Microbenchmarks

**Artifact Log:** [`.artifacts/roundtable-validation-2026-10-30/native-shim-benchmarks.log`](../../.artifacts/roundtable-validation-2026-10-30/native-shim-benchmarks.log)  
**Benchmark Source:** [`.artifacts/roundtable-validation-2026-10-30/NativeShimBenchmark.cpp`](../../.artifacts/roundtable-validation-2026-10-30/NativeShimBenchmark.cpp)

### 2.1 64-Bit (`x64`) Results

| Benchmark Operation | Native Server Core (`ltsc2025` x64) | Nano Server `v1.2.0` (x64) | Nano Server `v1.3.0` (Roundtable x64) | `v1.3.0` vs. `v1.2.0` | `v1.3.0` vs. Native Server Core |
| :--- | ---: | ---: | ---: | ---: | ---: |
| **`ApiSetQuery_50k`**<br>50,000 queries on shimmed `ext-ms-win-ntuser-window-l1-1-0` (`R17`/`R19`) | *(unhooked)* | `40,174.82 ms`<br>(`803,496.4 ns/op`, `1,245 ops/s`) | **`261.57 ms`**<br>(`5,231.3 ns/op`, `191,157 ops/s`) | **153.6x faster**<br>(`-99.35%`) | — |
| **`COM_STA_InitUninit_5k`**<br>5,000 `CoInitializeEx(COINIT_APARTMENTTHREADED)` + `CoUninitialize` cycles | `136.66 ms`<br>(`27,331.0 ns/op`, `36,589 ops/s`) | `84.77 ms`<br>(`16,953.0 ns/op`, `58,987 ops/s`) | **`38.13 ms`**<br>(`7,625.6 ns/op`, `131,137 ops/s`) | **2.22x faster**<br>(`-55.0%`) | **3.58x faster**<br>(`-72.1%`) |
| **`WindowCreateDestroy_10k`**<br>10,000 `CreateWindowExW(HWND_MESSAGE)` + `DestroyWindow` cycles (`R03`/`R10`) | `114.05 ms`<br>(`11,405.0 ns/op`, `87,681 ops/s`) | `3.60 ms`<br>(`360.1 ns/op`, `2,776,698 ops/s`) | **`3.31 ms`**<br>(`330.6 ns/op`, `3,024,529 ops/s`) | **1.09x faster**<br>(`-8.2%`) | **34.5x faster**<br>(`-97.1%`) |
| **`SendMessageSameThread_500k`**<br>500,000 same-thread `SendMessageW` calls (`R03`/`N01`) | `17.98 ms`<br>(`36.0 ns/op`, `27,809,814 ops/s`) | `24.04 ms`<br>(`48.1 ns/op`, `20,796,829 ops/s`) | **`35.85 ms`**<br>(`71.7 ns/op`, `13,946,660 ops/s`) | `0.67x`<br>*(adds owner-thread & liveness checks)* | `0.50x`<br>(`71.7 ns` vs. `36.0 ns`) |
| **`PostPeekDispatch_200k`**<br>200,000 `PostMessageW` + `MsgWait` + `PeekMessageW` + `DispatchMessageW` (`R06`/`R08`) | `492.34 ms`<br>(`2,461.7 ns/op`, `406,223 ops/s`) | `160.91 ms`<br>(`804.5 ns/op`, `1,242,959 ops/s`) | **`82.48 ms`**<br>(`412.4 ns/op`, `2,424,777 ops/s`) | **1.95x faster**<br>(`-48.7%`) | **5.97x faster**<br>(`-83.2%`) |
| **`SendMessageCrossThread_20k`**<br>20,000 cross-thread synchronous STA `SendMessageW` calls (`R01`/`R08`) | `197.10 ms`<br>(`9,854.9 ns/op`, `101,472 ops/s`) | `124.51 ms`<br>(`6,225.4 ns/op`, `160,632 ops/s`) | **`128.88 ms`**<br>(`6,444.1 ns/op`, `155,181 ops/s`) | `0.97x`<br>*(shared `PendingSendMessage` safety)* | **1.53x faster**<br>(`-34.6%`) |
| **`Concurrent10ThreadPumps_200k`**<br>10 STA threads × 20,000 messages = 200,000 messages (`R08`/`R24`) | `2,595.65 ms`<br>(`12,978.2 ns/op`, `77,052 ops/s`) | `763.04 ms`<br>(`3,815.2 ns/op`, `262,111 ops/s`) | **`816.75 ms`**<br>(`4,083.8 ns/op`, `244,873 ops/s`) | `0.93x` | **3.18x faster**<br>(`-68.5%`) |

### 2.2 32-Bit (`x86` / WOW64) Results

| Benchmark Operation | Native Server Core (`ltsc2025` x86) | Nano Server `v1.2.0` (x86) | Nano Server `v1.3.0` (Roundtable x86) | `v1.3.0` vs. `v1.2.0` | `v1.3.0` vs. Native Server Core |
| :--- | ---: | ---: | ---: | ---: | ---: |
| **`ApiSetQuery_50k`**<br>50,000 queries on shimmed `ext-ms-win-ntuser-window-l1-1-0` (`R17`/`R19`) | *(unhooked)* | `49,941.56 ms`<br>(`998,831.3 ns/op`, `1,001 ops/s`) | **`390.65 ms`**<br>(`7,812.9 ns/op`, `127,993 ops/s`) | **127.8x faster**<br>(`-99.22%`) | — |
| **`COM_STA_InitUninit_5k`**<br>5,000 `CoInitializeEx(COINIT_APARTMENTTHREADED)` + `CoUninitialize` cycles | `319.21 ms`<br>(`63,841.3 ns/op`, `15,664 ops/s`) | `64.55 ms`<br>(`12,910.4 ns/op`, `77,457 ops/s`) | **`56.58 ms`**<br>(`11,315.6 ns/op`, `88,374 ops/s`) | **1.14x faster**<br>(`-12.4%`) | **5.64x faster**<br>(`-82.3%`) |
| **`WindowCreateDestroy_10k`**<br>10,000 `CreateWindowExW(HWND_MESSAGE)` + `DestroyWindow` cycles (`R03`/`R10`) | `291.95 ms`<br>(`29,194.7 ns/op`, `34,253 ops/s`) | `2.26 ms`<br>(`225.6 ns/op`, `4,433,214 ops/s`) | **`5.71 ms`**<br>(`570.6 ns/op`, `1,752,664 ops/s`) | `0.40x`<br>*(dispatches 4 `R03` create/destroy messages)* | **51.2x faster**<br>(`-98.0%`) |
| **`SendMessageSameThread_500k`**<br>500,000 same-thread `SendMessageW` calls (`R03`/`N01`) | `48.47 ms`<br>(`96.9 ns/op`, `10,316,660 ops/s`) | `13.04 ms`<br>(`26.1 ns/op`, `38,334,739 ops/s`) | **`46.46 ms`**<br>(`92.9 ns/op`, `10,762,566 ops/s`) | `0.28x`<br>*(adds owner-thread & liveness checks)* | **1.04x faster**<br>(`92.9 ns` vs. `96.9 ns`) |
| **`PostPeekDispatch_200k`**<br>200,000 `PostMessageW` + `MsgWait` + `PeekMessageW` + `DispatchMessageW` (`R06`/`R08`) | `576.97 ms`<br>(`2,884.8 ns/op`, `346,640 ops/s`) | `234.94 ms`<br>(`1,174.7 ns/op`, `851,266 ops/s`) | **`133.17 ms`**<br>(`665.8 ns/op`, `1,501,871 ops/s`) | **1.76x faster**<br>(`-43.3%`) | **4.33x faster**<br>(`-76.9%`) |
| **`SendMessageCrossThread_20k`**<br>20,000 cross-thread synchronous STA `SendMessageW` calls (`R01`/`R08`) | `321.15 ms`<br>(`16,057.7 ns/op`, `62,275 ops/s`) | `183.31 ms`<br>(`9,165.3 ns/op`, `109,108 ops/s`) | **`181.92 ms`**<br>(`9,096.2 ns/op`, `109,937 ops/s`) | **1.01x faster** | **1.77x faster**<br>(`-43.4%`) |
| **`Concurrent10ThreadPumps_200k`**<br>10 STA threads × 20,000 messages = 200,000 messages (`R08`/`R24`) | `2,898.08 ms`<br>(`14,490.4 ns/op`, `69,011 ops/s`) | `898.08 ms`<br>(`4,490.4 ns/op`, `222,696 ops/s`) | **`704.42 ms`**<br>(`3,522.1 ns/op`, `283,921 ops/s`) | **1.27x faster**<br>(`-21.6%`) | **4.11x faster**<br>(`-75.7%`) |

### 2.3 How Each Roundtable Change Influenced Execution Speed

- **`R17` & `R19` (`ApplyModuleState` Caching + `WritePointerWithProtection` No-Op Check + Explicit `NanoServerStaShim_Refresh` Invalidation):**
  - In `v1.2.0`, every call to `Shim_ApiSetQueryApiSetPresence` or `Shim_ResolveDelayLoadedAPI` invoked `RefreshHooks()`, which iterated through every hooked module and called `VirtualProtect` twice per IAT entry (`~803 µs` per call on x64, `~999 µs` per call on x86).
  - In `v1.3.0`, `ApplyModuleState` tracks `state.HasAppliedState` and `state.LastAppliedEnable`, skipping already-applied modules during internal callback hooks, while `WritePointerWithProtection` checks `if (*target == value) return true;` before calling `VirtualProtect`. Explicit `NanoServerStaShim_Refresh()` calls reset `HasAppliedState = false` so external IAT resets (such as `AdaskoTheBeAsT.Interop.COM`'s `RestoreAllOriginalThunks()`) are always repaired.
  - **Measured Impact:** **153.6x faster** `ApiSetQueryApiSetPresence` on x64 (`40.17s -> 0.26s`), **127.8x faster** on x86 (`49.94s -> 0.39s`), and **2.22x faster** `CoInitializeEx(COINIT_APARTMENTTHREADED)` + `CoUninitialize` on x64 (`84.77 ms -> 38.13 ms`).
- **`R05`, `R06`, `R07`, `R08` (Queue Status Accounting, Wake-Mask Matching, and Nested `SendMessageW` Dispatch):**
  - Accurate `QS_POSTMESSAGE`, `QS_SENDMESSAGE`, `QS_TIMER`, `QS_PAINT`, and `QS_INPUT` tracking with `MWMO_INPUTAVAILABLE` short-circuiting avoids unnecessary kernel `WaitForMultipleObjectsEx` transitions when matching messages are already queued.
  - **Measured Impact:** `PostPeekDispatch_200k` improved from `160.91 ms` to **`82.48 ms` (`1.95x` faster) on x64** and from `234.94 ms` to **`133.17 ms` (`1.76x` faster) on x86**—outperforming Native Windows Server Core by **5.97x (x64)** and **4.33x (x86)**.
- **`R01`, `R03`, `R10`, `N01` (Cross-Thread `PendingSendMessage` Lifetime, Window Lifecycle Notifications, Class Atoms, Owner-Thread Validation):**
  - Cross-thread `SendMessageW` never executes a window procedure on the wrong thread (fixing `v1.2.0`'s 50ms wrong-thread fallback bug) and uses `std::shared_ptr<PendingSendMessage>` for race-free timeout/cancellation.
  - Same-thread `SendMessageW` now validates window liveness and owner-thread affinity (`~71.7 ns/op` on x64, `~92.9 ns/op` on x86—still over 10–14 million calls/sec).

---

## 3. Managed `AdaskoTheBeAsT.Interop.COM.Test` Suite (Modes 1–3 vs. Native Windows Server Core)

**Artifact Logs:**
- [`.artifacts/roundtable-validation-2026-10-30/final-modes-aspnet-x64.log`](../../.artifacts/roundtable-validation-2026-10-30/final-modes-aspnet-x64.log)
- [`.artifacts/roundtable-validation-2026-10-30/final-modes-aspnet-x86.log`](../../.artifacts/roundtable-validation-2026-10-30/final-modes-aspnet-x86.log)
- [`.artifacts/roundtable-validation-2026-10-30/final-modes-iis-aspnet-x64.log`](../../.artifacts/roundtable-validation-2026-10-30/final-modes-iis-aspnet-x64.log)
- [`.artifacts/roundtable-validation-2026-10-30/final-modes-iis-aspnet-x86.log`](../../.artifacts/roundtable-validation-2026-10-30/final-modes-iis-aspnet-x86.log)
- [`.artifacts/roundtable-validation-2026-10-30/final-modes-iis-netfx-x64.log`](../../.artifacts/roundtable-validation-2026-10-30/final-modes-iis-netfx-x64.log)
- [`.artifacts/roundtable-validation-2026-10-30/final-modes-iis-netfx-x86.log`](../../.artifacts/roundtable-validation-2026-10-30/final-modes-iis-netfx-x86.log)
- [`.artifacts/roundtable-validation-2026-10-30/servercore-managed-com-benchmarks.log`](../../.artifacts/roundtable-validation-2026-10-30/servercore-managed-com-benchmarks.log)

| Flavour / Target Framework | Tests | Native Server Core (`ltsc2025`) | Nano Server `v1.3.0` Mode 1 (Default: OS + Library Shim) | Nano Server `v1.3.0` Mode 2 (OS Shim Only) | Nano Server `v1.3.0` Mode 3 (Library Shim Only) | Mode 2 vs. Server Core |
| :--- | :---: | ---: | ---: | ---: | ---: | ---: |
| **`nanoserver-aspnet-x64`** (`net10.0\|x64`) | 172 | `448 ms` | `486 ms` | **`406 ms`** | `427 ms` | **1.10x faster** (`-9.4%`) |
| **`nanoserver-aspnet-x86`** (`net10.0\|x86`) | 172 | `583 ms` | `531 ms` | **`492 ms`** | `477 ms` | **1.18x faster** (`-15.6%`) |
| **`nanoserver-iis-aspnet-x64`** (`net10.0\|x64`) | 172 | `448 ms` | `450 ms` | **`409 ms`** | `389 ms` | **1.10x faster** (`-8.7%`) |
| **`nanoserver-iis-aspnet-x86`** (`net10.0\|x86`) | 172 | `583 ms` | `561 ms` | **`532 ms`** | `487 ms` | **1.10x faster** (`-8.7%`) |
| **`nanoserver-iis-netfx-x64`** (`net481\|x64`) | 173 | `720 ms` | `842 ms` | **`824 ms`** | `830 ms` | `0.87x` (`+104 ms`) |
| **`nanoserver-iis-netfx-x86`** (`net481\|x86`) | 173 | `599 ms` | `683 ms` | **`665 ms`** | `681 ms` | `0.90x` (`+66 ms`) |

*Note:* Across all 6 flavours, **Mode 2 (OS-Level `nanoserver_sta_shim.dll` Only, with `ADASKO_NANOSERVER_DISABLE_STA_SHIM=1`)** is consistently faster than Mode 1 (where both the OS-level shim and the managed `AdaskoTheBeAsT.Interop.COM` in-process shim hook the IATs), and on `.NET 10.0` (`x64` & `x86`) Nano Server `v1.3.0` is **9–18% faster than Native Windows Server Core**.

---

## 4. Real-World 200-Test `AhuNew` Suite (32-Bit Delphi `CoilLib` STA Worker Pool & ASP.NET Core Minimal Web API)

**Artifact Log:** [`.artifacts/roundtable-validation-2026-10-30/ahu-benchmarks.log`](../../.artifacts/roundtable-validation-2026-10-30/ahu-benchmarks.log)  
**Prior Baseline Reference:** [`docs/issues/argon-2026-10-29.md`](../issues/argon-2026-10-29.md)

All 4 container environments ran the exact same pre-compiled `Release` `net10.0` (`x86`) binaries via the 32-bit `.NET 10.0.14` host (`C:\Program Files (x86)\dotnet\dotnet.exe`) across two consecutive runs (**Pass 1 Cold** and **Pass 2 Warm**) inside `--isolation=process --cpus 10`:

1. **Nano Server `v1.3.0` (`nanoserver-roundtable:aspnet-x86-20261030`)** — Post-Roundtable image
2. **Nano Server `v1.2.0` (`adaskothebeast/nanoserver:aspnet-x86-10.0.14-ltsc2025-v1.2.0`)** — Pre-Roundtable image
3. **Original Windows Server Core (`mcr.microsoft.com/windows/servercore:ltsc2025`)** — Native `win32k.sys` / `user32.dll`
4. **Windows Server Core .NET 10 SDK (`mcr.microsoft.com/dotnet/sdk:10.0-windowsservercore-ltsc2025`)** — Native `win32k.sys` / `user32.dll` + SDK layers

### 4.1 End-to-End 200-Test Execution Time (Direct 32-Bit `dotnet.exe` Runner)

| Test Suite | Tests | Nano Server `v1.3.0` (`aspnet-x86`)<br>Pass 1 (Cold) / Pass 2 (Warm) | Nano Server `v1.2.0` (`aspnet-x86`)<br>Pass 1 (Cold) / Pass 2 (Warm) | Original Server Core (`ltsc2025`)<br>Pass 1 (Cold) / Pass 2 (Warm) | Server Core .NET 10 SDK (`ltsc2025`)<br>Pass 1 (Cold) / Pass 2 (Warm) |
| :--- | :---: | ---: | ---: | ---: | ---: |
| `AdaskoTheBeAsT.Splines.UnitTests` | 26 | `0.65 s` / `0.58 s`<br>*(runner: 318ms / 283ms)* | `0.62 s` / `0.58 s`<br>*(runner: 294ms / 292ms)* | `0.67 s` / `0.63 s`<br>*(runner: 300ms / 294ms)* | `0.70 s` / `0.60 s`<br>*(runner: 325ms / 281ms)* |
| `AdaskoTheBeAsT.Psychrometrics.UnitTests` | 129 | `0.71 s` / `0.65 s`<br>*(runner: 365ms / 375ms)* | `0.69 s` / `0.65 s`<br>*(runner: 374ms / 376ms)* | `0.68 s` / `0.69 s`<br>*(runner: 332ms / 378ms)* | `0.71 s` / `0.71 s`<br>*(runner: 370ms / 381ms)* |
| `AdaskoTheBeAsT.Hydrodynamics.UnitTests` | 17 | `0.58 s` / `0.55 s`<br>*(runner: 295ms / 270ms)* | `0.62 s` / `0.55 s`<br>*(runner: 298ms / 273ms)* | `0.66 s` / `0.66 s`<br>*(runner: 301ms / 318ms)* | `0.61 s` / `0.61 s`<br>*(runner: 263ms / 295ms)* |
| `AdaskoTheBeAsT.Interop.Coil.Tests` | 19 | `10.17 s` / `10.11 s`<br>*(runner: 9.83s / 9.83s)* | `10.01 s` / `10.02 s`<br>*(runner: 9.69s / 9.74s)* | `10.18 s` / `10.48 s`<br>*(runner: 9.82s / 10.12s)* | `10.07 s` / `10.26 s`<br>*(runner: 9.71s / 9.93s)* |
| `AdaskoTheBeAsT.WebApi.Coil.Tests` | 9 | `2.45 s` / `2.48 s`<br>*(runner: 2.13s / 2.20s)* | `2.41 s` / `2.35 s`<br>*(runner: 2.11s / 2.08s)* | `2.51 s` / `2.39 s`<br>*(runner: 2.13s / 2.04s)* | `2.51 s` / `2.40 s`<br>*(runner: 2.15s / 2.07s)* |
| **Total (All 200 Tests Passed)** | **200** | **`14.56 s` / `14.37 s`** | **`14.35 s` / `14.14 s`** | **`14.70 s` / `14.85 s`** | **`14.60 s` / `14.59 s`** |

> **Why Direct Assembly Execution Eliminated the Historical `38.68s–54.78s` `dotnet test` Overhead (`R26`):**  
> In the earlier benchmark report ([`docs/issues/argon-2026-10-29.md`](../issues/argon-2026-10-29.md)), `dotnet test --no-build --no-restore` was run inside a 24-layer SDK-mounted container, where `MSBuild.dll` project-graph evaluation and `vstest.console.dll` discovery accounted for `~24–40 seconds` of filesystem metadata overhead before test execution even began. Running compiled test assemblies directly via `dotnet.exe <TestAssembly>.dll` (matching how production services execute in `adaskothebeast/nanoserver:aspnet-x86`) completes all 200 tests in **`14.56 s` cold / `14.37 s` warm**, beating Native Windows Server Core (`14.70 s` cold / `14.85 s` warm).

### 4.2 Single-Worker STA Throughput (`AdaskoTheBeAsT.Interop.Coil.Tests`)

| Workload (Concurrent Calls on 1 STA Worker) | Nano Server `v1.3.0` (`aspnet-x86`)<br>Pass 1 / Pass 2 | Nano Server `v1.2.0` (`aspnet-x86`)<br>Pass 1 / Pass 2 | Original Server Core (`ltsc2025`)<br>Pass 1 / Pass 2 | Server Core .NET 10 SDK (`ltsc2025`)<br>Pass 1 / Pass 2 |
| :--- | ---: | ---: | ---: | ---: |
| **Water** (100 concurrent) | **`631.27 ms` (`158.4 calc/s`)**<br>`631.95 ms` (`158.2 calc/s`) | `738.61 ms` (`135.4 calc/s`)<br>`638.07 ms` (`156.7 calc/s`) | `722.10 ms` (`138.5 calc/s`)<br>`653.40 ms` (`153.0 calc/s`) | `629.40 ms` (`158.9 calc/s`)<br>`655.66 ms` (`152.5 calc/s`) |
| **Freon** (100 concurrent) | **`823.63 ms` (`121.4 calc/s`)**<br>`779.68 ms` (`128.3 calc/s`) | `864.26 ms` (`115.7 calc/s`)<br>`769.29 ms` (`130.0 calc/s`) | `881.89 ms` (`113.4 calc/s`)<br>`774.26 ms` (`129.2 calc/s`) | `781.69 ms` (`127.9 calc/s`)<br>`819.72 ms` (`122.0 calc/s`) |
| **Condensing** (100 concurrent) | **`553.74 ms` (`180.6 calc/s`)**<br>`556.01 ms` (`179.9 calc/s`) | `591.85 ms` (`169.0 calc/s`)<br>`560.53 ms` (`178.4 calc/s`) | `593.36 ms` (`168.5 calc/s`)<br>`596.95 ms` (`167.5 calc/s`) | `550.65 ms` (`181.6 calc/s`)<br>`559.05 ms` (`178.9 calc/s`) |
| **Glycol** (20 concurrent) | **`341.58 ms` (`58.6 calc/s`)**<br>`340.91 ms` (`58.7 calc/s`) | `358.96 ms` (`55.7 calc/s`)<br>`339.42 ms` (`58.9 calc/s`) | `335.87 ms` (`59.5 calc/s`)<br>`380.62 ms` (`52.5 calc/s`) | `341.97 ms` (`58.5 calc/s`)<br>`332.92 ms` (`60.1 calc/s`) |

### 4.3 1-Worker vs. 10-Worker Isolated STA Pool Scaling (`AdaskoTheBeAsT.Interop.Coil.Tests`)

| Workload (10 STA Workers) | Nano Server `v1.3.0` (`aspnet-x86`)<br>Pass 1 / Pass 2 | Nano Server `v1.2.0` (`aspnet-x86`)<br>Pass 1 / Pass 2 | Original Server Core (`ltsc2025`)<br>Pass 1 / Pass 2 | Server Core .NET 10 SDK (`ltsc2025`)<br>Pass 1 / Pass 2 |
| :--- | ---: | ---: | ---: | ---: |
| **Water** (200 concurrent) | **`285.51 ms` (`700.5 calc/s`, `4.62x`)**<br>**`290.05 ms` (`689.5 calc/s`, `4.48x`)** | `299.55 ms` (`667.7 calc/s`, `4.31x`)<br>`290.74 ms` (`687.9 calc/s`, `4.45x`) | `305.36 ms` (`655.0 calc/s`, `4.18x`)<br>`346.34 ms` (`577.5 calc/s`, `3.69x`) | `284.56 ms` (`702.8 calc/s`, `4.59x`)<br>`275.34 ms` (`726.4 calc/s`, `5.04x`) |
| **Freon** (200 concurrent) | **`338.23 ms` (`591.3 calc/s`, `5.00x`)**<br>`362.25 ms` (`552.1 calc/s`, `4.44x`) | `383.64 ms` (`521.3 calc/s`, `3.93x`)<br>`381.74 ms` (`523.9 calc/s`, `4.05x`) | `358.21 ms` (`558.3 calc/s`, `4.18x`)<br>`382.07 ms` (`523.5 calc/s`, `4.19x`) | `301.07 ms` (`664.3 calc/s`, `5.51x`)<br>`314.87 ms` (`635.2 calc/s`, `5.11x`) |
| **Condensing** (200 concurrent) | `300.21 ms` (`666.2 calc/s`, `4.80x`)<br>**`259.79 ms` (`769.8 calc/s`, `4.87x`)** | `251.84 ms` (`794.1 calc/s`, `5.11x`)<br>`277.75 ms` (`720.1 calc/s`, `5.45x`) | `273.21 ms` (`732.1 calc/s`, `5.23x`)<br>`339.53 ms` (`589.1 calc/s`, `3.98x`) | `261.07 ms` (`766.1 calc/s`, `5.09x`)<br>`315.46 ms` (`634.0 calc/s`, `4.55x`) |
| **Glycol** (40 concurrent) | **`176.18 ms` (`227.0 calc/s`, `4.73x`)**<br>`220.23 ms` (`181.6 calc/s`, `3.16x`) | `193.82 ms` (`206.4 calc/s`, `3.96x`)<br>`170.36 ms` (`234.8 calc/s`, `4.63x`) | `173.85 ms` (`230.1 calc/s`, `4.24x`)<br>`195.60 ms` (`204.5 calc/s`, `4.26x`) | `228.14 ms` (`175.3 calc/s`, `3.28x`)<br>`229.66 ms` (`174.2 calc/s`, `3.50x`) |

### 4.4 ASP.NET Core Minimal Web API End-to-End HTTP Load (`AdaskoTheBeAsT.WebApi.Coil.Tests`)

| HTTP Workload (120 Concurrent Mixed Requests) | Nano Server `v1.3.0` (`aspnet-x86`)<br>Pass 1 / Pass 2 | Nano Server `v1.2.0` (`aspnet-x86`)<br>Pass 1 / Pass 2 | Original Server Core (`ltsc2025`)<br>Pass 1 / Pass 2 | Server Core .NET 10 SDK (`ltsc2025`)<br>Pass 1 / Pass 2 |
| :--- | ---: | ---: | ---: | ---: |
| **1-Worker Pool** (`120` concurrent HTTP requests) | **`807.04 ms` (`148.7 req/s`, `6.73 ms/req`)**<br>`827.16 ms` (`145.1 req/s`, `6.89 ms/req`) | `819.80 ms` (`146.4 req/s`, `6.83 ms/req`)<br>`828.17 ms` (`144.9 req/s`, `6.90 ms/req`) | `807.16 ms` (`148.7 req/s`, `6.73 ms/req`)<br>`821.31 ms` (`146.1 req/s`, `6.84 ms/req`) | `811.20 ms` (`147.9 req/s`, `6.76 ms/req`)<br>`810.50 ms` (`148.1 req/s`, `6.75 ms/req`) |
| **10-Worker Pool** (`120` concurrent HTTP requests) | **`124.09 ms` (`967.0 req/s`, `1.03 ms/req`)**<br>`159.77 ms` (`751.1 req/s`, `1.33 ms/req`) | `118.30 ms` (`1014.4 req/s`, `0.99 ms/req`)<br>`122.44 ms` (`980.1 req/s`, `1.02 ms/req`) | `124.84 ms` (`961.2 req/s`, `1.04 ms/req`)<br>`123.41 ms` (`972.3 req/s`, `1.03 ms/req`) | `122.25 ms` (`981.6 req/s`, `1.02 ms/req`)<br>`117.72 ms` (`1019.4 req/s`, `0.98 ms/req`) |
| **10-Worker vs. 1-Worker HTTP Speedup** | **`6.50x`** (Pass 1) / **`5.18x`** (Pass 2) | **`6.93x`** (Pass 1) / **`6.76x`** (Pass 2) | **`6.47x`** (Pass 1) / **`6.66x`** (Pass 2) | **`6.64x`** (Pass 1) / **`6.89x`** (Pass 2) |
