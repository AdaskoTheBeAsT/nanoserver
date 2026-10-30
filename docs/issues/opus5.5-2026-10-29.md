# Opus 5.5 implementation review, 2026-10-29

## Verdict

**Not everything is implemented correctly.** I confirmed **22 actionable
findings: 3 P1, 13 P2, and 6 P3.** The managed test suite passes in all six
images (Astra ran all 24 modes against the current base images). However, the
native STA shim departs from Win32 in ways those tests never reach:

- It can run a window procedure on the wrong thread, run it twice, and let a
  late result land in freed stack memory.
- It never sends window creation or destruction messages and never fires timers.
- It can spin a CPU core inside `GetMessageW`.

The installer and several build scripts can also report success after a
failure. The README's "100% compatible" and W^X claims are not accurate, and
the Argon report's production-ready verdict is not supported.

**Scope.** I reviewed `main` at `885d75e` ("v1.2.0 changes"). That commit
landed while I was finishing this report. I reviewed the same content as
uncommitted changes on top of `d359366` (v1.1.0). Every file in the commit was
last modified before I built the probes and checked line references, so the
findings apply to `885d75e` as committed. The v1.2.0 changes cover the README,
the six push scripts, the shim sources and binaries, and `CHANGELOG.md`.
Coverage includes the native shim, export definition, installer, build script,
all six Dockerfiles and install scripts, the root and per-flavour scripts, the
test projects and runners, and the README. I did not change implementation
files. Review artifacts are in `.artifacts/opus5.5-2026-10-29/`, which git
ignores.

**Relationship to the other reviews.**

- [Sol](sol6.1-2026-10-29.md) (v1.1.0 snapshot) and
  [Astra](astra-2026-10-29.md) (current tree): every native and script finding
  they report is still present. I reproduced most of the native findings
  independently (see the probe table) and confirmed the `WM_QUIT` case by code
  inspection. I agree with their P1 ratings.
- New in this review: findings 10 and 15-22, plus new evidence or scope in
  findings 1, 2, 3, 6, 8, and 11.
- [Argon](argon-2026-10-29.md): its benchmarks show that one application
  workload runs at close to Server Core speed. Passing that workload does not
  test the Win32 behaviors where these defects are, so it does not support
  "everything is good and production-ready". Several of its line references are
  also offset from the current tree. For example, `GetOrCreateQueueLocked` is
  at lines 301-312 and `Shim_PeekMessageW` at 761-771.

## Findings at a glance

| # | Sev | Area | Finding | Also reported by |
| ---: | --- | --- | --- | --- |
| 1 | P1 | Shim | Cross-thread `SendMessageW` runs the handler on the wrong thread, can run it twice, and leaves a dangling stack pointer | Sol; Astra 1. New: double dispatch and stack write observed, racy timeout |
| 2 | P1 | Installer | Installer returns success after failed copies, patches, or registry changes | Sol; Astra 2. New: KnownDLLs and x86 paths |
| 3 | P1 | Shim | No creation or destruction messages; `DestroyWindow` leaves queued messages; unknown classes succeed | Sol; Astra 3. New: queued messages, thread check, unknown class |
| 4 | P2 | Shim | `SetWindowLongW` writes 8 bytes on x64 | Sol; Astra 4 |
| 5 | P2 | Shim | Timers never fire | Sol; Astra 5 |
| 6 | P2 | Shim | Wait and queue-status functions ignore masks and flags | Sol; Astra 6. New: `GetQueueStatus`, handle count |
| 7 | P2 | Shim | Filtered `GetMessageW` busy-spins | Astra 7 |
| 8 | P2 | Shim | Queues leak, pass to new threads through thread-ID reuse, and are created for any thread ID | Sol; Astra 8. New: ID reuse, `PostThreadMessageW` |
| 9 | P2 | Shim | `WM_QUIT` obeys message filters | Sol; Astra 9 |
| 10 | P2 | Shim | Window-class registration ignores Win32 rules | New |
| 11 | P2 | Images | DISM failures are swallowed; .NET Framework images cannot detect a missing WOW64 | Astra 11. New: .NET Framework check |
| 12 | P2 | Tests | Test runners treat crash exit codes as success | Sol; Astra 10 |
| 13 | P2 | Tooling | `push.ps1 -Version` fails in a fresh session | Sol; Astra 12 |
| 14 | P2 | Images | IIS containers keep running when IIS fails | Astra 13 |
| 15 | P2 | Docs | README misdescribes the native shim, including its security properties | New (Astra noted the missing sources) |
| 16 | P2 | Tests | Tests cannot detect native shim defects and assert some of them | New (Astra noted the coverage gaps) |
| 17 | P3 | Shim | IAT and cache writes make pages writable and executable | New |
| 18 | P3 | Installer | System DLLs are rewritten in place before the safe path is tried | New |
| 19 | P3 | Shim | Hook maintenance is costly and depends on build-specific byte patterns | New (Argon covers lock cost) |
| 20 | P3 | Shim | Smaller Win32 deviations | New |
| 21 | P3 | Build | No Control Flow Guard, no download hash checks, floating base images | New |
| 22 | P3 | Repo | Debug runtimes, personal metadata, and stale files | New |

## Native probe results

`.artifacts/opus5.5-2026-10-29/shim_harness.cpp` includes the shim source file
and calls the shim's functions directly. It therefore tests the implementation
itself, not IAT routing. Astra's probes through the real Win32 entry points
inside the current `aspnet-x64` and `aspnet-x86` images reproduced the
overlapping cases (T1, T2, T6, T7, T8, and T12), and its Server Core control
behaved correctly.

| Test | What it does | Win32 behavior | x64 result | x86 result |
| --- | --- | --- | --- | --- |
| T1 | `SetWindowLongW` at offset 4, then at offset 0 | Offset 4 keeps `0x22222222` | `0x00000000` (corrupted) | `0x22222222` (correct) |
| T2 | Create and destroy a window | `WM_NCCREATE`, `WM_CREATE`, `WM_DESTROY`, `WM_NCDESTROY` | 0 messages | 0 messages |
| T3 | Register the same class twice | Second call fails with error 1410 | Same atom, error 0, window procedure replaced | Same |
| T4 | Unregister a class that never existed | `FALSE` | `TRUE` | `TRUE` |
| T5 | `PostThreadMessageW` to thread `0xFFFFFFF0` | `FALSE`, error 1444 | `TRUE`; queue count goes from 1 to 2 | Same |
| T6 | Two 10 ms timers, pumped for 500 ms | Dozens of `WM_TIMER` messages or callbacks | 0 | 0 |
| T7 | `GetMessageW` with a window filter while only a thread message is queued | Blocks with no CPU use | 1000 ms CPU in 1000 ms | 984 ms CPU in 1000 ms |
| T8 | `MsgWaitForMultipleObjectsEx` with `QS_TIMER` and 1000 ms, only a posted message queued | `WAIT_TIMEOUT` after 1000 ms | Returns 0 after 0 ms | Same |
| T9 | `GetQueueStatus(QS_TIMER)`, only a posted message queued | 0 | `0x00FF00FF` | `0x00FF00FF` |
| T10 | Post to a window, destroy it, then peek | Message is gone | Message still returned | Same |
| T11 | `GetUserObjectInformationW(UOI_FLAGS)` | Fills `USEROBJECTFLAGS` | `TRUE`, buffer untouched, length 0 | Same |
| T12 | Cross-thread `SendMessageW`; owner alive but not pumping | Caller blocks; handler runs on the owner | Handler ran on the caller after 78 ms | Same, 78 ms |
| T13 | Cross-thread `SendMessageW`; owner pumping; handler takes 3 s | One call, on the owner | Ran twice; returned the caller's `0x1111` after 1,656 ms; owner's late result found in freed caller stack | Ran twice; returned after 94 ms; late result found in freed caller stack |

## P1 findings

### 1. [P1] Cross-thread `SendMessageW` breaks thread affinity and memory safety

**Location:** [`sta-shim/nanoserver_sta_shim.cpp:844-905`](../../sta-shim/nanoserver_sta_shim.cpp#L844);
receiver side at [372-392](../../sta-shim/nanoserver_sta_shim.cpp#L372).
**Evidence:** T12 and T13 on x64 and x86; code inspection.

The sender queues a pointer to a stack-allocated `SentMessageItem` (line 863)
on the owner's queue and polls for 25 or 500 ms. If the item has not completed
by then, the sender removes it from the queue if it is still there. Then, unless
`DoneEvent` is already set, it calls the window procedure itself on its own
thread (898-900).

That fallback causes three problems:

- **Wrong thread.** If the owner is busy or blocked outside a message wait, the
  handler runs on the sender (T12). For an STA window, the COM call then runs
  outside its apartment.
- **Double dispatch.** If the owner has already dequeued the item and is still
  running the handler (383-388), the sender runs the handler a second time
  (T13).
- **Use after return.** The sender then closes `DoneEvent` (903) and returns.
  When the owner finishes, it writes `item->Result` into the sender's released
  stack and calls `SetEvent` on the closed handle (388-390). If Windows has
  reused that handle value, an unrelated event can be signaled. T13 found the
  owner's late result `0x5A5A…` inside a buffer that the sender's thread
  allocated after `SendMessageW` returned.

The timeout is also unpredictable:

- The 500 ms branch depends on `WaitingCount` (874), which is read after
  `SetEvent` (873) has already woken the target. A target that wakes and
  decrements the counter first (784, 981) makes the sender choose 25 ms. With
  the owner waiting in `GetMessageW`, T13 on x64 returned after 78 ms in one run
  and after 1,656 ms in another.
- `elapsed += 5` (886) assumes each 5 ms wait lasts 5 ms. At the default
  15.6 ms timer resolution, the 25 ms budget lasts about 78 ms and the 500 ms
  budget about 1.6 s. Both match the measurements.

[`README.md:583`](../../README.md#L583) limits synchronous `SendMessageW` to
same-thread use. The implementation does not refuse cross-thread calls, though;
it runs them on the wrong thread.

**Fix direction:** Never run a window procedure on a thread that does not own
the window. Block as Win32 does, processing the sender's own incoming sent
messages while waiting. Also wait on the owner's thread handle so that a dead
owner releases the sender. Allocate the request on the heap with shared
ownership so a late receiver never touches freed memory. If a timeout is
needed, use `SendMessageTimeoutW` semantics: the caller gets a failure, and the
handler still runs once, on the owner.

### 2. [P1] The installer returns success after failed copies, patches, or registry changes

**Location:** [`sta-shim/install_sta_shim.cpp:615-622`](../../sta-shim/install_sta_shim.cpp#L615),
[664-697](../../sta-shim/install_sta_shim.cpp#L664), and
[574-591](../../sta-shim/install_sta_shim.cpp#L574).
**Evidence:** code inspection. Sol and Astra reproduced the copy-failure case.

- `CopyShimDll` reports nothing to its caller and ignores the second copy
  entirely (621). `wmain` ignores every `PatchPeFileImports`,
  `PatchPeFileExports`, and `CopyFileW` result and always returns 0 (697). A
  missing payload leaves system DLLs importing a DLL that is not there, and
  Docker still commits the layer.
- `RemoveKnownDllEntries` only prints a warning when it cannot open the
  KnownDLLs key, and it ignores each `RegDeleteValueW` result (582-590). If the
  entries remain, the loader keeps mapping the original KnownDLL sections. The
  patched files are then never loaded, and the shim is silently inactive.
- The whole x86 half is skipped without a message when
  `C:\Windows\SysWOW64\forwarders` is missing (682). In x86 flavours, a failed
  WOW64 install therefore produces no installer error. Finding 11 shows where
  the Dockerfiles do not catch this either.

**Fix direction:** Check that both payloads exist before touching any system
file. Treat every copy, patch, and KnownDLLs change as required, stop at the
first failure, and return a nonzero exit code. Re-read the patched files to
verify the import and export changes. Make the x86 requirement explicit, for
example with a command-line switch that the x86 and .NET Framework Dockerfiles
pass.

### 3. [P1] Windows get no creation or destruction messages, and `DestroyWindow` leaves state behind

**Location:** [`sta-shim/nanoserver_sta_shim.cpp:534-606`](../../sta-shim/nanoserver_sta_shim.cpp#L534)
and [526-532](../../sta-shim/nanoserver_sta_shim.cpp#L526).
**Evidence:** T2 and T10 on x64 and x86; code inspection. Sol and Astra
reproduced the missing messages, and Astra's Server Core control delivered them.

- `CreateWindowExW` discards `lpParam` (551) and returns a handle without
  sending `WM_NCCREATE` or `WM_CREATE`. Window procedures that store their
  object pointer from `CREATESTRUCT.lpCreateParams`, the usual pattern in C++
  window wrappers, never initialize.
- `DestroyWindow` erases the entry without sending `WM_DESTROY` or
  `WM_NCDESTROY` (594-606). Cleanup code never runs. The common pattern of
  calling `PostQuitMessage` from `WM_DESTROY` never ends the message loop.
- Messages already posted to the window stay in the queue and are returned later
  with the dead handle (T10).
- `DestroyWindow` accepts windows owned by other threads. Win32 fails with
  `ERROR_ACCESS_DENIED`.
- `CreateWindowExW` succeeds for a class name that was never registered and
  uses `DefWindowProcW` instead (554-568). Win32 fails with
  `ERROR_CANNOT_FIND_WND_CLASS`.

**Fix direction:** Send `WM_NCCREATE` and `WM_CREATE` synchronously with a real
`CREATESTRUCTW`, and fail creation when the window procedure rejects them. Send
`WM_DESTROY` and `WM_NCDESTROY` before removing the entry, purge the window's
posted messages, and enforce the owner-thread rule. Fix `DefWindowProcW` at the
same time: it returns 0 for every message, so once `WM_NCCREATE` is delivered,
every window procedure that forwards it to `DefWindowProcW` would abort its own
creation. Win32 returns `TRUE` there.

## P2 findings

### 4. [P2] `SetWindowLongW` writes 8 bytes on x64

**Location:** [`nanoserver_sta_shim.cpp:723-729`](../../sta-shim/nanoserver_sta_shim.cpp#L723),
delegating to [708-716](../../sta-shim/nanoserver_sta_shim.cpp#L708) and
[660-664](../../sta-shim/nanoserver_sta_shim.cpp#L660).
**Evidence:** T1 fails on x64 and passes on x86. Sol and Astra reproduced it.

`SetWindowLongW` and `GetWindowLongW` call the pointer-sized implementations.
On x64, writing a 32-bit field also overwrites the next 4 bytes. Reading the
last 4-byte field of the extra area returns 0, because the read requires 8
bytes (661). The index is never checked against `cbWndExtra`: any nonnegative
index grows the buffer (710-713). A very large index can allocate gigabytes or
throw `std::bad_alloc` out of a `WINAPI` function while `g_WindowLock` is held.

**Fix direction:** Read and write `sizeof(LONG)` in the `Long` variants,
validate the index against the class's `cbWndExtra`, and fail with
`ERROR_INVALID_INDEX`.

### 5. [P2] Timers never fire

**Location:** [`nanoserver_sta_shim.cpp:930-954`](../../sta-shim/nanoserver_sta_shim.cpp#L930).
**Evidence:** T6 on x64 and x86. Sol and Astra reproduced it.

`SetTimer` and `SetCoalescableTimer` return an ID and store nothing, and
`KillTimer` always returns `TRUE`. No `WM_TIMER` message or `TIMERPROC` callback
is ever produced, so code that waits for a timer stalls. The README lists timers
as supported ([208](../../README.md#L208), [523](../../README.md#L523),
[580](../../README.md#L580), [583](../../README.md#L583)).

**Fix direction:** Keep a per-thread timer list. Generate `WM_TIMER` when a
timer is due and no other posted message is waiting, as Win32 does. Shorten wait
timeouts to the next due time. Make `KillTimer` remove the timer and return
`FALSE` for unknown IDs.

### 6. [P2] Wait and queue-status functions ignore what the caller asked for

**Location:** [`nanoserver_sta_shim.cpp:956-985`](../../sta-shim/nanoserver_sta_shim.cpp#L956)
and [907-911](../../sta-shim/nanoserver_sta_shim.cpp#L907).
**Evidence:** T8 and T9 on x64 and x86. Sol and Astra reproduced the wake-mask
and `MWMO_WAITALL` cases.

- `MsgWaitForMultipleObjectsEx` ignores `dwWakeMask` (962). Any queued message
  satisfies any mask (T8).
- If anything is queued, it returns `nCount` before it looks at the handles or
  `MWMO_WAITALL` (965-967).
- When `nCount` is above 63, it silently waits on only the first 63 handles
  (970) and gives the queue event index 63. A message wake is then reported as
  `WAIT_OBJECT_0 + 63`, which the caller reads as "handle 63 signaled". Win32
  rejects such calls with `ERROR_INVALID_PARAMETER`.
- `GetQueueStatus` ignores its flags and returns `0x00FF00FF` whenever anything
  is queued (T9). Callers that test specific bits, such as `QS_SENDMESSAGE`,
  take the wrong branch.

Together with finding 7, these turn ordinary wait loops into busy loops. For
example, a wait for sent messages returns at once on every iteration while an
unrelated posted message sits in the queue.

**Fix direction:** Classify queued work (posted, sent, timer, and quit) and
test it against the mask. Apply `MWMO_WAITALL` and `MWMO_INPUTAVAILABLE` as
documented, reject `nCount` values above 63, and compute the `GetQueueStatus`
bits from the actual queue contents.

### 7. [P2] Filtered `GetMessageW` busy-spins

**Location:** [`nanoserver_sta_shim.cpp:773-786`](../../sta-shim/nanoserver_sta_shim.cpp#L773);
event resets at [372-379](../../sta-shim/nanoserver_sta_shim.cpp#L372) and
[425-427](../../sta-shim/nanoserver_sta_shim.cpp#L425).
**Evidence:** T7 on x64 and x86. Astra reproduced it.

The queue's wake event is manual-reset and is reset only when the queue is
empty. When none of the queued messages match the caller's filter,
`GetMessageW` cannot dequeue any of them, and its 50 ms wait returns
immediately on every iteration. In T7, waiting for a window message while a
thread message was queued burned 1000 ms of CPU in 1000 ms on x64 and 984 ms on
x86.

**Fix direction:** Wait for "a new message arrived since the last check", not
"the queue is not empty". A sequence number with a condition variable, or an
auto-reset event per waiter, would work.

### 8. [P2] Thread queues leak, pass to new threads through thread-ID reuse, and are created for any thread ID

**Location:** [`nanoserver_sta_shim.cpp:301-312`](../../sta-shim/nanoserver_sta_shim.cpp#L301),
[830-838](../../sta-shim/nanoserver_sta_shim.cpp#L830), and
[1693-1704](../../sta-shim/nanoserver_sta_shim.cpp#L1693).
**Evidence:** T5 on x64 and x86; code inspection. Sol and Astra measured the
leak (64 event handles for 64 threads).

- Each thread that touches a queue gets a `ThreadMessageQueue` and an event,
  and neither is ever freed. `DllMain` disables thread notifications (1696), so
  there is no cleanup point.
- Queues are found by thread ID, and Windows reuses thread IDs. A new thread
  that receives a recycled ID inherits the dead thread's queue and windows,
  including undelivered messages and pending `SendMessageW` items that point
  into a dead thread's stack. This is an inference from the code; I did not
  reproduce it.
- `PostThreadMessageW` returns `TRUE` for any thread ID and allocates a queue
  for it (T5). Win32 returns `FALSE` with `ERROR_INVALID_THREAD_ID`, which
  callers use to detect a dead worker thread.

**Fix direction:** Release the queue when its thread exits, for example with a
fiber-local storage callback (`FlsAlloc`), which runs even when thread
notifications are disabled. At that point, also destroy the thread's windows
and fail its pending sends. Return `FALSE` from `PostThreadMessageW` for threads
that do not exist.

### 9. [P2] `WM_QUIT` obeys message filters

**Location:** [`nanoserver_sta_shim.cpp:416-420`](../../sta-shim/nanoserver_sta_shim.cpp#L416);
posted at [840-842](../../sta-shim/nanoserver_sta_shim.cpp#L840).
**Evidence:** code inspection. Sol and Astra reproduced it: a filtered
`PeekMessageW` after `PostQuitMessage` returned `FALSE`. I did not add a separate
probe.

Win32 documents that `GetMessage` and `PeekMessage` always retrieve `WM_QUIT`,
whatever message-range filter is passed. It also keeps the quit request as a
queue flag that is returned after other posted messages. The shim queues
`WM_QUIT` as an ordinary message and applies both the window filter and the
range filter to it, so a filtered message loop can miss shutdown.

**Fix direction:** Keep a quit flag per queue and return `WM_QUIT` when no other
posted message matches, regardless of the range filter.

### 10. [P2] Window-class registration ignores Win32 rules

**Location:** [`nanoserver_sta_shim.cpp:443-479`](../../sta-shim/nanoserver_sta_shim.cpp#L443),
[509-524](../../sta-shim/nanoserver_sta_shim.cpp#L509), and
[321-342](../../sta-shim/nanoserver_sta_shim.cpp#L321).
**Evidence:** T3 and T4 on x64 and x86; code inspection.

- Registering an existing class name succeeds, returns the same atom, and
  replaces the window procedure, styles, and extra-byte sizes for windows
  created afterward (T3). Win32 fails with `ERROR_CLASS_ALREADY_EXISTS`.
- `hInstance` is ignored for registration, lookup, and unregistration. Win32
  keeps application-local classes from different modules apart, so two DLLs
  that happen to use the same private class name both work. In the shim, the
  last registration silently wins for both.
- `UnregisterClassW` returns `TRUE` for unknown classes (T4) and removes classes
  that still have windows. Win32 fails with `ERROR_CLASS_DOES_NOT_EXIST` or
  `ERROR_CLASS_HAS_WINDOWS`.
- Atoms are `0xC000 | (counter & 0x3FFF)` (467), and the counter grows with
  every new registration, including re-registration after `UnregisterClassW`.
  After 16,384 such registrations, two classes can share an atom, and lookups by
  atom (329-334) return the first match.

**Fix direction:** Key classes by name and `hInstance`. Fail duplicate
registrations, and fail unregistration of unknown or in-use classes. Reuse freed
atoms instead of wrapping.

### 11. [P2] DISM failures are swallowed, and the .NET Framework images cannot detect a missing WOW64

**Location:** lines 4-7 and 14-24 of `install_features.cmd` in the four IIS
flavours, for example
[`nanoserver-iis-netfx-x86/install_features.cmd:4`](../../nanoserver-iis-netfx-x86/install_features.cmd#L4);
[`nanoserver-iis-netfx-x86/Dockerfile:37-40`](../../nanoserver-iis-netfx-x86/Dockerfile#L37)
and [line 56](../../nanoserver-iis-netfx-x86/Dockerfile#L56), with the same
structure in `nanoserver-iis-netfx-x64/Dockerfile`.
**Evidence:** code inspection. Astra reproduced the script behavior with mocked
DISM exit codes.

- The script ignores both `:AddCap` results and ends with `exit /b 0`. Inside
  `:AddCap`, `if errorlevel 3010` treats every code of 3010 or more as a reboot
  request, and negative DISM codes pass the `if errorlevel 1` check.
- New in this review: the .NET Framework images check for 32-bit IIS with
  `if exist C:\Windows\SysWOW64\inetsrv\w3wp.exe` (line 56). That file was just
  copied from the Server Core stage (line 39). The `COPY` steps at lines 37 and
  39 also create `C:\Windows\SysWOW64` when WOW64 is not installed, and the shim
  installer then skips x86 without an error (finding 2). A failed WOW64 install
  therefore yields an x86 .NET Framework image that builds successfully but
  cannot start 32-bit worker processes. The ASP.NET Core x86 images check for
  `C:\Windows\SysWOW64` before copying anything into it
  ([`nanoserver-aspnet-x86/Dockerfile:20`](../../nanoserver-aspnet-x86/Dockerfile#L20),
  [`nanoserver-iis-aspnet-x86/Dockerfile:42`](../../nanoserver-iis-aspnet-x86/Dockerfile#L42)),
  so they catch this case.

**Fix direction:** Accept only 0 and 3010 as success, and propagate every other
result from each capability call. In the .NET Framework images, check for a
file that only the WOW64 capability provides, such as
`C:\Windows\SysWOW64\ntdll.dll`, before the `COPY` steps.

### 12. [P2] Test runners treat crash exit codes as success

**Location:** lines 12, 21, 30, and 39 of `Tests/run_all_modes.cmd` in all six
flavours, for example
[`nanoserver-aspnet-x64/Tests/run_all_modes.cmd:12`](../../nanoserver-aspnet-x64/Tests/run_all_modes.cmd#L12).
**Evidence:** I confirmed that `if errorlevel 1` is false after exit code
`-1073741819` (`0xC0000005`, access violation). Sol and Astra reproduced the
full runner printing `ALL 4 MODES PASSED SUCCESSFULLY!` after crashes.

**Fix direction:** After each run, use
`if not "%errorlevel%"=="0" exit /b %errorlevel%`.

### 13. [P2] `push.ps1 -Version` fails in a fresh PowerShell session

**Location:** [`push.ps1:20-21`](../../push.ps1#L20), with
`Set-StrictMode -Version Latest` at line 12.
**Evidence:** `.artifacts/opus5.5-2026-10-29/lastexitcode_check.ps1` reproduces
the strict-mode error when no native command has run. Sol and Astra reproduced
it with the real script.

`set-version.ps1` runs no native executable, so `$LASTEXITCODE` is unset, and
strict mode throws after the version files have already been rewritten. In a
session where an earlier native command failed, the stale value causes a false
failure instead.

**Fix direction:** Remove the check. The script already sets
`$ErrorActionPreference = "Stop"` (line 13), so failures in `set-version.ps1`
terminate it.

### 14. [P2] IIS containers keep running when IIS fails

**Location:** `CMD` at
[`nanoserver-iis-aspnet-x64/Dockerfile:85`](../../nanoserver-iis-aspnet-x64/Dockerfile#L85),
[`nanoserver-iis-aspnet-x86/Dockerfile:87`](../../nanoserver-iis-aspnet-x86/Dockerfile#L87),
[`nanoserver-iis-netfx-x64/Dockerfile:71`](../../nanoserver-iis-netfx-x64/Dockerfile#L71),
and [`nanoserver-iis-netfx-x86/Dockerfile:71`](../../nanoserver-iis-netfx-x86/Dockerfile#L71).
**Evidence:** code inspection. Astra reproduced it with mocked executables.

`net start w3svc >nul 2>&1 & ping -t localhost` hides startup errors and runs
`ping` forever whether or not IIS started. If W3SVC stops later, the container
keeps running, so restart policies and orchestrators never see a failure.

**Fix direction:** Exit with an error when `net start` fails, and run a
foreground monitor that exits when W3SVC stops. Microsoft's IIS images use
`ServiceMonitor.exe` for this; check that it runs on Nano Server, or use a small
polling script.

### 15. [P2] The README misdescribes the native shim, including its security properties

**Evidence:** code inspection.

| README | Claim | What the code does |
| --- | --- | --- |
| [569](../../README.md#L569) | "No executable heap (`PAGE_EXECUTE_READWRITE`) is ever allocated. IAT patching temporarily sets `PAGE_READWRITE`…" | Every IAT and cache write switches the page to `PAGE_EXECUTE_READWRITE` ([285-299](../../sta-shim/nanoserver_sta_shim.cpp#L285)). See finding 17. |
| [568](../../README.md#L568) | `g_windows` and `g_queues` protected by `SRWLOCK` / `ConcurrentDictionary` | The native shim uses `std::vector` tables named `g_Windows` and `g_ThreadQueues`, scanned linearly under two `CRITICAL_SECTION` locks ([142-150](../../sta-shim/nanoserver_sta_shim.cpp#L142)). |
| [519](../../README.md#L519) | Redirects `IsRegisterClassWPresent()` to `Shim_AlwaysTrue()` | No such function exists. The shim hooks the `ApiSetQueryApiSetPresence(Ex)` imports ([1277-1308](../../sta-shim/nanoserver_sta_shim.cpp#L1277)) and overwrites presence caches that it finds with a byte-pattern scan ([1367-1449](../../sta-shim/nanoserver_sta_shim.cpp#L1367)). |
| [481](../../README.md#L481), [485](../../README.md#L485), [523](../../README.md#L523), [580](../../README.md#L580) | The C++ engine has a window property bag (`SetPropW`, `GetPropW`, `RemovePropW`) and timers | The native function table ([1178-1225](../../sta-shim/nanoserver_sta_shim.cpp#L1178)) has no property functions, and timers do nothing (finding 5). |
| [208](../../README.md#L208), [580](../../README.md#L580) | "Works 100%", "100% Compatible" | Contradicted by findings 1-10. |
| [21](../../README.md#L21), [55-56](../../README.md#L55) | Pulls build-matched CABs (`10.0.26100.32522`) and installs them offline with DISM | The Dockerfiles run `DISM /Online /Add-Capability`, which installs Features on Demand from Windows Update for whatever build the base image is. Nothing pins `10.0.26100.32522`, and nothing is installed offline. [`CHANGELOG.md:79`](../../CHANGELOG.md#L79) describes this correctly. |
| [90](../../README.md#L90) | `Tests\src\NativeCOM\`, `Tests\src\NativeLifetimeProbe\`, and `Tests\src\AdaskoTheBeAsT.Interop.COM\` contain the sources | None of the six `Tests` folders has a `src` directory. The tests use the `AdaskoTheBeAsT.Interop.COM` 4.2.0 NuGet package and prebuilt native binaries. Astra noted this too. |
| [217](../../README.md#L217) | With both opt-outs set, `CoInitializeEx(…, COINIT_APARTMENTTHREADED)` returns `E_NOTIMPL` | No test checks this (finding 16). |

The installer's header comment
([`install_sta_shim.cpp:2-3`](../../sta-shim/install_sta_shim.cpp#L2)) also says
it patches `combase.dll`, `user32.dll`, and `ole32.dll`. In fact it patches the
`user32`, `ole32`, `gdi32`, and `comctl32` forwarders and never touches
`combase.dll`.

**Fix direction:** Correct these claims. Add a table of the native APIs the shim
supports, the ones it does not, and the known deviations.

### 16. [P2] The tests cannot detect native shim defects, and some tests assert them

**Location:** [`NanoServerWindowManagerTest.cs:354-370`](../../nanoserver-aspnet-x64/Tests/AdaskoTheBeAsT.Interop.COM.Test/NanoServerWindowManagerTest.cs#L354),
[412-417](../../nanoserver-aspnet-x64/Tests/AdaskoTheBeAsT.Interop.COM.Test/NanoServerWindowManagerTest.cs#L412),
[425-426](../../nanoserver-aspnet-x64/Tests/AdaskoTheBeAsT.Interop.COM.Test/NanoServerWindowManagerTest.cs#L425);
[`NanoServerFallbackTest.cs:382-424`](../../nanoserver-aspnet-x64/Tests/AdaskoTheBeAsT.Interop.COM.Test/NanoServerFallbackTest.cs#L382);
[`run_all_modes.cmd:38`](../../nanoserver-aspnet-x64/Tests/run_all_modes.cmd#L38).
The same files exist in all six flavours.
**Evidence:** code inspection.

- The window-manager tests call the managed engine
  (`NanoServerWindowManager.*Impl`) directly. Nothing calls the native shim's
  window functions; its `NanoServerStaShim_Refresh`,
  `NanoServerStaShim_RestoreOriginal`, or `NanoServerStaShim_IsActive` exports;
  or the eight v1.2.0 GDI, user32, and comctl32 stubs.
- Some tests require the defective behavior. Lines 354-370 expect cross-thread
  `SendMessage` to "time out and invoke directly" (finding 1). Lines 412-417
  check timer IDs but never delivery (finding 5). Lines 425-426 expect a wait
  with a zero wake mask and a null handle array to report a message
  (finding 6).
- Mode 4, "Both Shims Disabled at Startup + Runtime Toggle Verification", runs a
  single test. It never calls `CoInitializeEx(COINIT_APARTMENTTHREADED)`, so the
  README's `E_NOTIMPL` claim is unverified. It sets and clears
  `NANOSERVER_DISABLE_STA_SHIM` but never checks native behavior, and it turns
  the managed shim back on at lines 405-410.

The "174/174 and 175/175" result in [`README.md:222`](../../README.md#L222) is
accurate (Astra reproduced it), but it says little about the native shim.

**Fix direction:** Add a native probe executable to each test image. It should
call the real `user32` and `ole32` entry points on x64 and x86, assert Win32
behavior for the cases in the probe table, and run against a Server Core
control. Add tests for the installer's failure paths. Make mode 4 assert
`E_NOTIMPL` and exercise the native exports.

## P3 findings

### 17. [P3] IAT and cache writes make pages writable and executable

**Location:** [`nanoserver_sta_shim.cpp:285-299`](../../sta-shim/nanoserver_sta_shim.cpp#L285).

For each write, the shim switches IAT slots (read-only data) and presence
caches (writable data) to `PAGE_EXECUTE_READWRITE`. These are data pages, so
execute permission is unnecessary, and the switch briefly creates writable,
executable memory in every process that loads the shim. It also contradicts
README line 569. In processes that enable Arbitrary Code Guard, Windows refuses
to make pages executable. Because the code simply skips the write when
`VirtualProtect` fails, the hook then silently does not apply. (The Arbitrary
Code Guard behavior is an inference; I did not test it.)

**Fix direction:** Use `PAGE_READWRITE`, and report or count failed writes.

### 18. [P3] The installer rewrites system DLLs in place before it tries the safe path

**Location:** [`install_sta_shim.cpp:151-168`](../../sta-shim/install_sta_shim.cpp#L151),
[226-242](../../sta-shim/install_sta_shim.cpp#L226),
[193-206](../../sta-shim/install_sta_shim.cpp#L193), and
[512](../../sta-shim/install_sta_shim.cpp#L512).

- Both writers first open the target with `CREATE_ALWAYS`, which truncates it,
  and write in place. The temp-file-and-rename path runs only if that fails. A
  short write leaves a truncated system DLL. If the fallback then fails too, the
  function returns `false`, which nobody checks (finding 2). The fallback's
  `.orig` "backup" is a copy of the already-truncated file, and it is deleted
  anyway (206, 278).
- The export patcher drops everything after the last section (512). That is
  fine for the Authenticode signature it removes on purpose, but other trailing
  data is not checked. Before adding a section header, it also does not check
  for data stored in the header area after the section table, such as a bound
  import directory (433-438). Neither occurs in the current Nano Server
  forwarders, so this is a robustness gap rather than a current failure.
- Dead code: `CopyForwardersIfMissing` (593-613) and, in the shim,
  `g_OrigCoInitializeEx`
  ([`nanoserver_sta_shim.cpp:162`](../../sta-shim/nanoserver_sta_shim.cpp#L162)).

**Fix direction:** Always write to a temporary file, flush it, and replace the
target with `ReplaceFileW` or
`MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`. Keep the
backup until the result has been verified.

### 19. [P3] Hook maintenance is costly and depends on build-specific byte patterns

**Location:** [`nanoserver_sta_shim.cpp:1277-1308`](../../sta-shim/nanoserver_sta_shim.cpp#L1277),
[1547-1597](../../sta-shim/nanoserver_sta_shim.cpp#L1547),
[1367-1449](../../sta-shim/nanoserver_sta_shim.cpp#L1367), and
[273-283](../../sta-shim/nanoserver_sta_shim.cpp#L273).

- Every presence query for a shimmed API set runs `RefreshHooksLocked`, which
  rewrites every tracked IAT slot with two `VirtualProtect` calls each.
- `ApplyModuleState` writes the discovery-time value back into every slot that
  has no shim target (1574) and clears every delay-load module-handle slot
  (1577-1579), whether it is enabling or disabling. Slots that the delay-load
  helper resolved in the meantime are reset, so the next call resolves them
  again and loads the module again.
- `IsOsShimDisabledByEnv` reads the environment variable on every presence query
  and every delay-load resolution.
- `DiscoverPresenceCaches` finds the presence caches in `combase`, `user32`, and
  `ole32` by matching the exact instruction bytes of the current OS build. If a
  cumulative update changes the generated code, the scan finds nothing and
  nothing reports it. Enabling or disabling the shim at run time then no longer
  affects cached presence results.

Argon's report covers the cost of the single global lock, which I did not
measure.

**Fix direction:** Refresh only when a module loads (the loader notification
already does this) and on explicit `Refresh` calls. Cache the environment check
and update it on `Refresh`. Expose diagnostics such as the number of caches
found, and fail a container smoke test when it is zero.

### 20. [P3] Smaller Win32 deviations

- `GetUserObjectInformationW` returns `TRUE` without writing the buffer for
  every index except `UOI_NAME`, and it reports a required length of 0
  ([1011-1016](../../sta-shim/nanoserver_sta_shim.cpp#L1011); T11). Callers that
  read `USEROBJECTFLAGS` get whatever their buffer already held.
- `DefWindowProcW` returns 0 for every message
  ([526-532](../../sta-shim/nanoserver_sta_shim.cpp#L526)). This blocks the fix
  for finding 3 (see there), and `WM_CLOSE` does not destroy the window.
- `DispatchMessageW` calls any nonzero `WM_TIMER` `lParam` as a `TIMERPROC`
  ([797-801](../../sta-shim/nanoserver_sta_shim.cpp#L797)) without checking that
  a timer was registered with it. Because timers never fire, every `WM_TIMER`
  here was posted by code, and a posted `WM_TIMER` whose `lParam` is not a
  callback crashes the dispatching thread.
- The v1.2.0 GDI stubs return fixed fake handles (`0x53484D01` to `0x53484D04`),
  and `CopyEnhMetaFileA` returns the source handle itself
  ([1085-1171](../../sta-shim/nanoserver_sta_shim.cpp#L1085)). The matching
  release functions (`DeleteEnhMetaFile`, `DeleteObject`, `DestroyIcon`) are not
  shimmed. The CHANGELOG describes these as minimal headless stubs, so this is
  not a defect in itself, but what callers may do with these handles should be
  documented and tested.

### 21. [P3] Build and supply-chain hardening

- [`build_shim.ps1:31, 38, 42`](../../sta-shim/build_shim.ps1#L31) build
  without `/guard:cf`. The DLL characteristics are `0x160` on x64 and `0x140` on
  x86: dynamic base, NX, and (x64 only) high-entropy addresses, but no Control
  Flow Guard. This DLL loads into every process in these images that uses
  `user32`, `ole32`, `gdi32`, or `comctl32`. The sources compile cleanly at
  `/W4`, so the build could also use `/W4 /WX`.
- The .NET runtime ZIPs and the ASP.NET Core Module MSI are downloaded without
  checksum verification, for example at
  [`nanoserver-aspnet-x64/Dockerfile:15`](../../nanoserver-aspnet-x64/Dockerfile#L15),
  [`nanoserver-aspnet-x86/Dockerfile:25`](../../nanoserver-aspnet-x86/Dockerfile#L25),
  [`nanoserver-iis-aspnet-x64/Dockerfile:15`](../../nanoserver-iis-aspnet-x64/Dockerfile#L15),
  and [`nanoserver-iis-aspnet-x86/Dockerfile:15`](../../nanoserver-iis-aspnet-x86/Dockerfile#L15),
  plus the download lines that follow them. Microsoft's official .NET
  Dockerfiles verify the published SHA-512 hashes.
- All base images use floating tags. The .NET Framework images copy the whole
  Server Core `System32\inetsrv` and `SysWOW64\inetsrv` trees over Nano Server's
  IIS ([`nanoserver-iis-netfx-x86/Dockerfile:39-40`](../../nanoserver-iis-netfx-x86/Dockerfile#L39)).
  The IIS binaries therefore come from whatever Server Core build the SDK tag
  points to at build time, which can differ from the Nano Server base build.

**Fix direction:** Enable `/guard:cf` for both compiler and linker, verify
download hashes, and either pin base image digests or check that both build
stages report the same OS build.

### 22. [P3] Repository hygiene

- Debug C runtime DLLs (`ucrtbased.dll`, `vcruntime140d.dll`, and
  `vcruntime140_1d.dll`) are committed in every `Tests\native` folder and copied
  into `System32` or `SysWOW64` of the test images
  ([`nanoserver-aspnet-x64/Tests/Dockerfile:17-18`](../../nanoserver-aspnet-x64/Tests/Dockerfile#L17)).
  `NativeCOM.dll` is a Debug build: it imports `VCRUNTIME140D.dll` and
  `ucrtbased.dll` and contains `.msvcjmc` and `.textbss` sections. Microsoft
  does not allow redistribution of the debug runtimes. Build the test server in
  Release with the static runtime instead.
- The x86 `NativeCOM.manifest` (lines 7-9) contains generator licensing metadata
  that identifies a person, and the x64 manifest (line 6) describes the build
  environment. Remove both. I have not reproduced that content here.
- `nanoserver-aspnet-x86/install_runtime.cmd` is not referenced anywhere.
- `nanoserver-iis-aspnet-x86` copies the x86 ASP.NET Core Module to
  `Program Files (x86)`
  ([Dockerfile:63](../../nanoserver-iis-aspnet-x86/Dockerfile#L63)) but
  registers only the x64 module (line 68), and the app pool stays 64-bit
  (lines 71-72). The x86 files are never used. Out-of-process hosting starts the
  x86 `dotnet.exe` through `PATH`, which works, but the design should be
  documented.
- `Tests/Directory.Build.props:7` sets `Version` 4.1.0, while the tests reference
  package version 4.2.0. `Tests/global.json` uses `rollForward: latestMajor`
  with `allowPrerelease: true`, so test builds can silently pick up a newer
  major or preview SDK.

## Validation

What I ran:

- Compiled the shim at `/W4` for x64 and x86, and the installer at `/W4` for
  x64, with MSVC 14.51.36231 and Windows SDK 10.0.28000. There were no warnings.
- Built `shim_harness.cpp` for x64 and x86 and ran it twice. Both runs showed
  the same deviations; only timings varied, most visibly T13's return time. The
  second run's output is saved as `x64/harness_output.txt` and
  `x86/harness_output.txt`.
- Ran `dumpbin` on the shipped binaries. The shim exports 14 names: the three
  loader and API set hooks, the three control functions, and the eight v1.2.0
  stubs. Window functions are not exported; they are resolved through the
  internal table. The installer imports only `KERNEL32` and `ADVAPI32`. The test
  DLLs depend on the debug runtimes. `ntdll` and `kernelbase` export
  `ApiSetQueryApiSetPresence(Ex)` and `ResolveDelayLoadedAPI`, so the shim's
  run-time lookups succeed.
- Confirmed in `cmd` that `if errorlevel 1` is false after exit code
  `-1073741819`.
- Confirmed in PowerShell that reading an unset `$LASTEXITCODE` under strict
  mode throws.

What I did not run:

- No Docker image builds or container runs. For container evidence, I rely on
  Astra's 24 test-mode runs on the current base images and its native probes
  inside the current `aspnet-x64` and `aspnet-x86` images.
- No live HTTP request through IIS in any image. None of the earlier reviews
  reports one through IIS either.
- No performance measurements.
- No review of the managed library's internals; its source is not in the
  repository.
- The antimalware scan interface (AMSI) blocked a PowerShell probe that used
  P/Invoke. I did not try to bypass it and used `dumpbin` instead.

The harness calls the shim's functions directly, so it does not test IAT
routing, loader notifications, or the installer's patching. Only Astra's
in-image probes and the managed test runs cover those paths.

## Suggested order of work

1. Fix `SendMessageW` (finding 1), and fix window lifecycle together with
   `DefWindowProcW` (finding 3).
2. Make the installer and the DISM scripts fail closed (findings 2 and 11), and
   fix the test-runner exit checks (finding 12) so that later regressions are
   visible.
3. Add native probe tests (finding 16) before changing more behavior, so each
   fix is measured.
4. Implement timers, wait masks, queue status, `WM_QUIT`, and the filtered-wait
   fix (findings 5, 6, 7, and 9).
5. Fix queue lifetime and class registration (findings 8 and 10), then
   `SetWindowLongW` (finding 4).
6. Correct the README (finding 15), then work through the P3 items.

## Local evidence

All files are under `.artifacts/opus5.5-2026-10-29/`, which git ignores:

- `shim_harness.cpp`: probe source for T1-T13.
- `x64/` and `x86/`: harness builds, `harness_output.txt`, and the `/W4` build
  logs (`w4_shim.txt`, `w4_installer.txt` for x64, and `build_harness.txt`).
- `shim64_headers.txt`, `shim86_headers.txt`, `shim64_exports.txt`,
  `shim64_imports.txt`, and `installer_imports.txt`: `dumpbin` output for the
  shipped binaries.
- `nativecom64_deps.txt`, `nativecom86_deps.txt`, and `probe64_deps.txt`: test
  DLL dependencies.
- `ntdll_exports.txt` and `kernelbase_exports.txt`: host export lists used to
  check the shim's run-time lookups.
- `lastexitcode_check.ps1`: strict-mode `$LASTEXITCODE` reproduction.
