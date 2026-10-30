# Four-seat roundtable: combined findings and agreed fixes, 2026-10-29

Four reviewers, each the author of one earlier review in this folder, argued
their findings against each other over three rounds and converged on one list.
This document is the result, ratified by all four seats. Where it contradicts
the individual reports ([Sol](sol6.1-2026-10-29.md),
[Astra](astra-2026-10-29.md), [Argon](argon-2026-10-29.md),
[Opus 5.5](opus5.5-2026-10-29.md)), this document wins.

Tree under review: `main` at `885d75e` ("v1.2.0 changes").

## Verdict

**Not ready**, by all four seats, for release as reusable base images for
hosting legacy 32-bit Delphi and VCL native engines under ASP.NET Core.

| Blocks release | Seats |
| --- | --- |
| R01, R02, R03, R11a, R11b, R12, R15a, R16 | all four |
| R14, for the four IIS images only | all four |
| R04 to R10, and N01 | Argon, Sol, Astra. Opus would accept them as known deviations if R15a documents them. |

Argon measured one workload at close to Server Core speed: the 32-bit
`CoilLib.dll`, `COIL.dll`, and `Selecting.dll` engines hosted by
`AhuNew` on `aspnet-x86`, each engine pinned to one of ten long-lived STA
threads. Argon and Opus record `ready with conditions` for that workload only,
under five conditions:

1. Each built image is verified after the build, because the installer cannot
   report failure (R02).
2. STA threads form a fixed, long-lived pool and are never created per request
   (R08).
3. No thread other than a window's owner calls `SendMessageW` on that window
   (R01).
4. No component relies on creation or destruction messages, on `lpParam`, or on
   timers (R03, R05).
5. Message pumps are unfiltered, and x64 code does not call `SetWindowLongW` on
   extra window bytes (R04, R06, R07, R09, N01).

The repository does not state these conditions as a deployment contract and
enforces none of them. The README comes closest on condition 3:
[`README.md:583`](../../README.md#L583) limits synchronous `SendMessageW` to the
same thread. The code does not enforce that limit, because a cross-thread send
runs the handler on the caller instead of failing (R01). Sol and Astra treat
Argon's measurement as bounded evidence and do not endorse the conditional
approval.

## How this document was produced

| Seat | Model | Authored | Definition |
| --- | --- | --- | --- |
| Argon | `gemini-4-argon` | [argon-2026-10-29.md](argon-2026-10-29.md) | [`reviewer-argon.md`](../../.factory/droids/reviewer-argon.md) |
| Sol 6.1 | `gpt-6.1-sol` | [sol6.1-2026-10-29.md](sol6.1-2026-10-29.md) | [`reviewer-sol61.md`](../../.factory/droids/reviewer-sol61.md) |
| Astra | `gpt-6-astra` | [astra-2026-10-29.md](astra-2026-10-29.md) | [`reviewer-astra.md`](../../.factory/droids/reviewer-astra.md) |
| Opus 5.5 | `claude-opus-5-5` | [opus5.5-2026-10-29.md](opus5.5-2026-10-29.md) | [`reviewer-opus55.md`](../../.factory/droids/reviewer-opus55.md) |

| Step | What happened |
| --- | --- |
| Round 1, positions | Each seat answered the challenges to its own report and reviewed a shared seed list of 26 findings. Argon withdrew its production-ready headline and corrected 18 stale line references. Opus withdrew one finding, lowered one, split two, merged two, and moved one to an appendix. Six new findings were nominated, N01 to N06. |
| Round 2, rebuttal | Each seat voted on an 18-item ballot about the list's structure, answered four questions that code settles, and gave final severities. Sol, Astra, and Opus voted identically on 15 of 18 items. |
| Round 2b, correction | Argon's round 2 note used its own numbering, R01 to R37 with different meanings, and cited functions that do not exist in the shim. Sent back, Argon withdrew every unresolvable citation, withdrew two P1 claims that rested on absent code, folded the third into R04, and mapped all 37 rows to shared IDs. No new finding survived. |
| Chair audit | The chair re-read every line this document cites, ran the batch `errorlevel` cases at the shell, and corrected wording where the code differs from what a seat said. |
| Round 3, ratification | Argon ratified the document as written. Sol, Astra, and Opus ratified it with exceptions. The chair verified every exception against the code and applied them. None changes the verdict or a blocker. See Sign-offs. |

Round notes are in `.artifacts/roundtable-2026-10-29/`, which git ignores.

## Evidence labels

- **chair-verified**: the chair read the cited lines in the current tree while
  writing this document.
- **shell-verified**: the chair ran the case in `cmd.exe` on the review host.
- **seat-verified**: a seat read or ran it; the chair did not repeat it.
- **in-image**: reproduced inside a current container image, through the real
  Win32 entry points, with Server Core as a control (Astra).
- **harness**: reproduced by a harness that includes the shim source directly,
  so it bypasses the import table, the loader, and the installer (Opus, tests
  T1 to T13).
- **inferred**: follows from code that was read, but was not executed.
- **unverified**: nobody established it.

## Ballot results

| Item | Question | Argon | Sol | Astra | Opus | Outcome |
| --- | --- | --- | --- | --- | --- | --- |
| A | Merge R19 and R25 | yes | yes | yes | yes | Merged into R19 |
| B | Merge R18 into R02 | no | no | no | no | Kept separate |
| C | R26 is intended design, not a defect | yes | yes | yes | yes | Performance note |
| D | R20 keeps `UOI_FLAGS`, `DefWindowProcW` moves to R03, the TIMERPROC allegation is dropped | yes | yes | yes | yes | Adopted |
| E | Drop R22 | yes | yes | yes | yes | Dropped |
| F | Move R21 to a hardening appendix | yes | yes | yes | yes | Appendix A |
| G | R23, R24, R26 become performance notes, R24 after R08 | yes | yes | yes | yes | Appendix B |
| H | Split R11 into R11a and R11b | yes | yes | yes | yes | Split |
| I | Split R15 into R15a (P2) and R15b (P3), `E_NOTIMPL` unverified | yes | yes | yes | yes | Split |
| J | Restate R16 as "the suites assert no native contract" | yes | no | no | yes | Literal wording rejected; all four accept the defect-specific wording in R16 |
| K | R13 severity | P3 | P3 | P3 | P3 | P3 |
| L | Keep R01 as one finding, evidence tagged by source | yes | yes | yes | yes | One finding |
| M | R03 fix purges posted messages on destroy | abstain | abstain | abstain | control first | No blanket purge until a Server Core control is run |
| N | R05 uses lazy `GetTickCount64` deadlines, no background threads | yes | yes | yes | yes | Adopted |
| O | R10 ships with `GetClassInfo(Ex)W` and the identity cases | yes | yes | yes | yes | Adopted |
| P | R02 stays P1 although it is build-time | yes | yes | yes | yes | P1 |
| Q | Blocker set | full | full | full | core | Full set, 3 of 4 |
| R | Verdict | not ready | not ready | not ready | not ready | Not ready |

"Full" is R01 to R10, R11a, R11b, R12, R15a, R16, and N01. "Core" leaves out R04
to R10 and N01. Both add R14 for the IIS images.

## Findings at a glance

| ID | Sev | Blocks release | Area | Finding |
| --- | --- | --- | --- | --- |
| R01 | P1 | all four | Shim | Cross-thread `SendMessageW` queues a stack-allocated request, can run the handler twice, and lets the owner write into a dead stack frame |
| R02 | P1 | all four | Installer | The installer discards every copy, patch, and registry result and always exits 0 |
| R03 | P1 | all four | Shim | Windows get no creation or destruction messages, `lpParam` is dropped, any thread can destroy any window, and unregistered classes succeed |
| R04 | P2 | 3 of 4 | Shim | `SetWindowLongW` writes 8 bytes on x64, and any non-negative index grows the extra-byte buffer instead of failing |
| R05 | P2 | 3 of 4 | Shim | Timers never fire: `SetTimer` records nothing and `KillTimer` does nothing |
| R06 | P2 | 3 of 4 | Shim | `MsgWaitForMultipleObjectsEx` ignores the wake mask, skips `MWMO_WAITALL` when a message is queued, and mishandles more than 63 handles; `GetQueueStatus` returns a constant |
| R07 | P2 | 3 of 4 | Shim | A filtered `GetMessageW` burns CPU without blocking once one unmatched message is queued |
| R08 | P2 | 3 of 4 | Shim | Thread queues are never freed, a recycled thread id inherits the old queue, and `PostThreadMessageW` succeeds for any id |
| R09 | P2 | 3 of 4 | Shim | `WM_QUIT` is an ordinary filtered message, so a filtered pump never exits |
| R10 | P2 | 3 of 4 | Shim | Re-registering a class overwrites it, unregistering always succeeds, and atoms repeat after 16384 registrations |
| R11a | P2 | all four | Images | `install_features.cmd` cannot fail: the callers ignore `:AddCap`, whose guards also pass codes of 3010 and above and all negative HRESULTs |
| R11b | P2 | all four | Images | The .NET Framework images look for `w3wp.exe` in a directory an earlier `COPY` created, so the check cannot fail |
| R12 | P2 | all four | Tests | All six test runners use `if errorlevel 1`, which is false for native crash codes |
| R13 | P3 | no | Tooling | `push.ps1 -Version` reads `$LASTEXITCODE` under strict mode before any native command has run |
| R14 | P2 | IIS images | Images | The IIS `CMD` ignores whether `net start w3svc` succeeded and keeps the container alive with `ping -t` |
| R15a | P2 | all four | Docs | The README claims STA support "Works 100%", W^X compliance, and "100% Compatible" message pumping; none holds |
| R15b | P3 | no | Docs | Other README and installer-comment descriptions do not match the code |
| R16 | P2 | all four (scoped) | Tests | No test asserts the native behaviour behind R01 to R10 |
| R17 | P3 | no | Shim | Hook writes switch pages to `PAGE_EXECUTE_READWRITE`, ignore failures, and are not serialized |
| R18 | P3 | no | Installer | The direct write truncates the live system DLL before anything is staged, so a later rollback restores a truncated file |
| R19 | P3 | no | Shim | Hook state is rewritten on every enabled query for a shimmed namespace, and the pattern scan reads and writes through addresses it never range-checks |
| R20 | P3 | no | Shim | `GetUserObjectInformationW(UOI_FLAGS)` returns success without filling the buffer |
| N01 | P2 | 3 of 4 | Shim | A pump filtered on a window never sees messages for its child windows, and `IsChild` checks only the direct parent |
| N02 | P3, provisional | no | Shim | No ANSI window or message functions are shimmed |
| N03 | P3 | no | Shim | `CallWindowProcW` is absent; `GetClassInfoW` and `GetClassInfoExW` are too, and are counted in R10 |
| N05 | P3 | no | Shim | Clipboard format ids leave the 16-bit range from the 16384th registration on |

Split severities: N01 is P2 for Argon, Sol, and Astra, and P3 for Opus. N02 is
P3 for Argon and Opus; Astra holds it as a P2 candidate that does not block, and
Sol would leave it unranked until a probe shows a failure. N03 is P3 for Argon,
Astra, and Opus; Sol would leave it unranked.

Outside the defect count: R21 (build hardening, Appendix A) and R23, R24, R26
(performance notes, Appendix B). Withdrawn: R22, N04, N06 (Appendix C). R25 is
merged into R19.

## P1 findings

### R01 Cross-thread `SendMessageW` is memory-unsafe

**Evidence:** chair-verified at
[`nanoserver_sta_shim.cpp:844-905`](../../sta-shim/nanoserver_sta_shim.cpp#L844)
and [`:372-392`](../../sta-shim/nanoserver_sta_shim.cpp#L372). In-image for the
wrong-thread dispatch. Harness (T13) for the double dispatch and the late write.

The defect is worse than running a handler on the wrong thread.

1. The lock is released at `:853`, right after reading the owner's thread id, so
   the window can be destroyed before the request is queued.
2. `SentMessageItem item;` at `:863` is a **stack local**, and its address is
   pushed onto the owner's queue at `:872`.
3. The timeout depends on whether the owner happened to be blocked in a wait at
   that instant (`:874-877`): 500 ms if it was, 25 ms if not. The loop adds 5
   per pass at `:886`, so it counts passes, not time.
4. On timeout the caller removes its request at `:890-897`. If the owner already
   dequeued it at `:383-384` and is still inside the handler at `:388`, the
   removal finds nothing, the done event is not set yet (`:898`), and the caller
   runs the handler **a second time** at `:899`.
5. The caller then closes the event at `:903` and returns. The owner afterwards
   writes `item->Result` at `:388` and signals the closed handle at `:390`, into
   a stack frame that no longer exists. Handle values are reused, so that
   `SetEvent` can also wake an unrelated waiter whose event took the same value
   (inferred).

If the owner had not dequeued the request yet, the caller runs the handler
itself at `:899`, on the wrong thread. That is the case Astra reproduced in an
image.

**Agreed fix** (Sol's design, endorsed by Argon, Astra, and Opus): only the
owner thread runs the handler. Allocate the request on the heap with shared
ownership, so neither side can free it while the other still holds it. Keep
servicing incoming sends while waiting. If the window is destroyed or the owner
thread exits before dispatch starts, cancel the request and wake the sender with
a failure result. Once dispatch has started, the sender stays blocked until the
handler returns, or until the owner thread is confirmed gone: the handler may
destroy its own window and still be using data that `wParam` or `lParam` points
to in the sender's frame. Serialize cancellation against dispatch. Remove both
the wrong-thread fallback and the timeout: `SendMessageW` has no timeout in
Win32, and `SendMessageTimeoutW` is not a prerequisite.

### R02 The installer cannot fail

**Evidence:** chair-verified at
[`install_sta_shim.cpp:574-591`](../../sta-shim/install_sta_shim.cpp#L574),
[`:615-622`](../../sta-shim/install_sta_shim.cpp#L615), and
[`:664-697`](../../sta-shim/install_sta_shim.cpp#L664).

`wmain` returns 0 at `:697` whatever happened. Its only nonzero exit is the
guard against running on a non-Nano host at `:629`. Every `PatchPeFileImports`,
`PatchPeFileExports`, and `CopyFileW` call at `:667-677` and `:684-694` is a bare
statement whose result is discarded. `CopyShimDll` returns `void`, uses the
first copy's result only to decide whether to print at `:618-620`, and ignores
the second copy into `forwarders\` at `:621`. `RemoveKnownDllEntries` discards
each `RegDeleteValueW` result at `:583-585` and prints `[OK]` at `:587` anyway.
If the key cannot be opened, it warns at `:589` and carries on. If those values
survive, the loader keeps using the original KnownDLL sections and the shim is
silently inactive (inferred). The whole x86 half is gated at `:682` on
`C:\Windows\SysWOW64\forwarders` existing, and is skipped without a message when
it does not. A Docker `RUN` step commits its layer on exit 0, so a broken image
is published as a good one.

**Agreed fix** (Sol's and Astra's designs, which agree; the explicit
architecture switch was proposed by Opus as `/require-x86` and by Argon as
`--require-wow64`): before changing any system file, check every payload and
destination that the selected architectures need. An x64-only image needs
neither the x86 payload nor WOW64. Make the required architectures explicit, so
a missing `forwarders` directory is an error where x86 is required and an
expected absence elsewhere. Treat every copy, patch, and KnownDLLs deletion as
required, stop at the first failure, and return nonzero. A KnownDLLs value that
is already absent counts as success. Re-read the patched files to confirm the
import and export edits landed. Safe replacement and rollback belong to R18.

### R03 Windows have no lifecycle

**Evidence:** chair-verified at
[`nanoserver_sta_shim.cpp:534-592`](../../sta-shim/nanoserver_sta_shim.cpp#L534),
[`:594-606`](../../sta-shim/nanoserver_sta_shim.cpp#L594), and
[`:526-532`](../../sta-shim/nanoserver_sta_shim.cpp#L526). In-image for the
missing creation and destruction messages and the dropped `lpParam`.
Unregistered classes and destruction from another thread are code reading only.

`CreateWindowExW` discards `lpParam` at `:551`, sends neither `WM_NCCREATE` nor
`WM_CREATE`, and returns a handle at `:591`. A class that was never registered
still produces a window: the lookup at `:558-568` finds nothing and leaves
`Shim_DefWindowProcW` in place from `:554`. `DestroyWindow` at `:594-606` erases
the entry and returns. It sends no `WM_DESTROY` or `WM_NCDESTROY`, does not check
that the caller owns the window, and leaves messages already queued for the
handle. `Shim_DefWindowProcW` returns 0 for every message at `:526-532`. That
includes `WM_NCCREATE`, where Win32 returns `TRUE`, and `WM_CLOSE`, where Win32
destroys the window.

**Agreed fix** (Astra's design, endorsed by Sol, Opus, and Argon): a real
lifecycle, sent synchronously on the owning thread and without holding the
global window lock, so window procedures can reenter the shim.

- Pass a `CREATESTRUCTW` carrying `lpParam`. If the window procedure fails
  `WM_NCCREATE`, abort creation and clean up.
- Reject an unregistered class with `ERROR_CANNOT_FIND_WND_CLASS`. Keep a
  defined set of predefined classes that callers create without registering,
  for example `Message`, `#32768` to `#32772`, `STATIC`, `BUTTON`, and `EDIT`.
  `OleMainThreadWndClass` needs no special case: combase registers it itself
  through the shimmed `RegisterClassExW`, and the R03 and R10 probes must cover
  that registration and the window combase creates.
- Fail `DestroyWindow` from a thread that does not own the window. Follow the
  Win32 order: send the parent `WM_DESTROY` while its children still exist,
  destroy the children, then send the parent `WM_NCDESTROY` and remove it.
  Remove the window's timers.
- `DefWindowProcW` returns `TRUE` for `WM_NCCREATE` and destroys the window on
  `WM_CLOSE`.

The seats did not prescribe purging already-posted messages for a destroyed
window, because no Server Core control has established what Windows does with
them (ballot M). The minimum invariant all four accept: no window procedure runs
for a destroyed window. Today it holds only when the window is destroyed on its
owning thread. A lookup after removal finds no procedure (`:364-368`), and
handles are never reused (`:572`). But `GetWndProcForWindow` releases the lock
at `:360` before the call at `:369`, and `DestroyWindow` may run on any thread,
so a destroy from another thread can land between the two. Owner-only
destruction (this fix) and owner-only sends (R01) make the invariant
unconditional. A test should then lock it in.

## P2 findings

### R04 `SetWindowLongW` writes 8 bytes and grows the buffer

**Evidence:** chair-verified at
[`:670-729`](../../sta-shim/nanoserver_sta_shim.cpp#L670) and
[`:586-587`](../../sta-shim/nanoserver_sta_shim.cpp#L586). In-image on x64.

`Shim_SetWindowLongW` forwards to the pointer-width setter at `:728`, which
writes a full `LONG_PTR` at `:715`. On x64 a 4-byte API therefore overwrites the
next 4 bytes as well. The index is never checked against the class's declared
`cbWndExtra`: any non-negative index resizes the buffer to fit at `:710-712`,
where Win32 fails the call. Creation already rounds every window up to at least
64 bytes at `:586`.

The resize also runs while `g_WindowLock` is held (taken at `:671`, released at
`:719`). On x86, a large index makes it throw `std::bad_alloc` out of a `WINAPI`
function. If the caller catches the exception, for example in a Delphi
`try`/`except`, the lock stays owned and every other thread blocks on its next
shim call; if nothing catches it, the process ends (inferred). This is the
out-of-memory item Argon folded into R04 in round 2b.

**Agreed fix** (Sol's, endorsed by Astra, Opus, and Argon): width-specific
access, 4 bytes for the `LONG` functions and pointer width for the `LONG_PTR`
functions, bounded by the declared `cbWndExtra`, never growing on write, and
failing with `ERROR_INVALID_INDEX` out of range. The bound removes the
`bad_alloc` path. Give the getters the same width and bounds rules.

### R05 Timers never fire

**Evidence:** chair-verified at
[`:930-954`](../../sta-shim/nanoserver_sta_shim.cpp#L930). In-image.

`Shim_SetTimer` discards the window, the interval, and the callback at
`:931-933`. It returns the caller's id at `:934-935`, or a new one at `:937`,
and records nothing. `Shim_KillTimer` ignores both arguments and returns `TRUE`
at `:950-953`. No `WM_TIMER` is ever queued and no callback ever runs.

**Agreed fix** (Argon's design, endorsed by the other three; ballot N): keep
timer records per owner, and check `GetTickCount64()` deadlines lazily on the
owning thread inside the dequeue and wait paths, with no background timer
thread. With no timers, the check should reduce to an empty-set test, but that
overhead is unmeasured. Bound blocking waits by the nearest deadline. Generate
`WM_TIMER` at low priority, only when no posted message matches. Support
replacement by id, cancellation before and after the first tick, and removal
when the window or thread goes away.

### R06 Wait and queue-status functions ignore their arguments

**Evidence:** chair-verified at
[`:956-985`](../../sta-shim/nanoserver_sta_shim.cpp#L956) and
[`:907-911`](../../sta-shim/nanoserver_sta_shim.cpp#L907). In-image for the
ignored wake mask and the queued-message `MWMO_WAITALL` case. The
`GetQueueStatus` constant is harness (T9) plus code reading.

`dwWakeMask` is discarded at `:962`. Any queued message returns `nCount` at
`:965-967`, before the `MWMO_WAITALL` handling at `:976` is reached, so a
wait-all caller whose handles are unsignaled is told it is ready.
`MWMO_INPUTAVAILABLE` is never handled. The handle count is clamped to 63 at
`:970`, and the queue's wake event goes into the next slot at `:974`. With 64 or
more handles, the 64th and later are never waited on and no error is reported.
Worse, a message arrival then returns `WAIT_OBJECT_0 + 63` (`:980`, `:984`),
which the caller reads as its 64th handle being signaled rather than as input.
`GetQueueStatus` discards its flags at `:908` and returns the constant
`0x00FF00FF` whenever anything is queued, at `:910`.

**Agreed fix** (Astra's, endorsed by Argon and Opus): keep typed queue-status
bits per message category. Validate `nCount` and `pHandles` before any readiness
check, and fail `nCount` above 63 with `ERROR_INVALID_PARAMETER`, which also
removes the misreported index. Apply the wake mask and the flags on every path,
including the already-queued one. `GetQueueStatus` reports only the requested
categories, in both its current and changed halves.

### R07 A filtered `GetMessageW` burns CPU without blocking

**Evidence:** chair-verified at
[`:773-786`](../../sta-shim/nanoserver_sta_shim.cpp#L773),
[`:309`](../../sta-shim/nanoserver_sta_shim.cpp#L309),
[`:376-379`](../../sta-shim/nanoserver_sta_shim.cpp#L376), and
[`:425-427`](../../sta-shim/nanoserver_sta_shim.cpp#L425). In-image (Astra):
281 ms of CPU in 300 ms of wall clock on x64 and 265 ms on x86, against 0 ms on
Server Core. Harness (T7): 1000 ms in 1000 ms on x64 and 984 ms on x86.

This is not a tight spin by construction: the loop waits on the queue's wake
event with a 50 ms timeout at `:783`. The event is manual-reset (`TRUE` at
`:309`) and is only cleared when the queue is completely empty, at `:378` and
`:426`. One queued message that does not match the caller's filter therefore
leaves the event signaled forever. The wait returns at once on every pass, and
the thread uses close to a full core for as long as it pumps.

**Agreed fix** (Sol's, endorsed by Astra, Argon, and Opus): wait on a
queue-change counter captured under the lock after the matching check, so a
wake-up cannot be lost between the check and the wait. Simply resetting the
event when nothing matches is not a fix: it loses wake-ups for sent messages and
new arrivals.

### R08 Thread queues outlive their threads

**Evidence:** chair-verified at
[`:301-312`](../../sta-shim/nanoserver_sta_shim.cpp#L301),
[`:822`](../../sta-shim/nanoserver_sta_shim.cpp#L822),
[`:830-838`](../../sta-shim/nanoserver_sta_shim.cpp#L830),
[`:860`](../../sta-shim/nanoserver_sta_shim.cpp#L860), and
[`:1693-1704`](../../sta-shim/nanoserver_sta_shim.cpp#L1693). In-image on the
current images: the process handle count grew by 64 after 64 pump threads came
and went, on both x64 and x86, against 0 on Server Core (Astra). Sol reported
the same count earlier, on the v1.1.0 snapshot.

`GetOrCreateQueueLocked` allocates a queue and a wake event for any thread id at
`:307-310`. Nothing ever frees them. `DllMain` turns thread notifications off
with `DisableThreadLibraryCalls` at `:1696`, there is no other thread-exit hook,
and the only `CloseHandle` in the file is at `:903`, on a send request's event.
A recycled thread id therefore inherits the dead thread's queue, backlog
included. `PostThreadMessageW` creates a queue and returns `TRUE` for any id at
`:830-838`, where Win32 fails with `ERROR_INVALID_THREAD_ID`. `PostMessageW` and
`SendMessageW` also create the window owner's queue on demand, at `:822` and
`:860`.

**Agreed fix** (Astra's, endorsed by Argon and Opus, with Opus's completion):
only the owning thread creates its queue, and it registers the queue for
cleanup with an FLS callback at that moment, because an FLS value can only be
set on its own thread. Every cross-thread path (`:822`, `:832`, `:860`) looks up
an existing queue and never creates one; a live thread without a queue is
rejected with error 1444. At thread exit, free the queue safely with respect to
waiters and pending sends, which needs R01's shared-ownership change, and remove
that thread's windows without calling their window procedures, so later posts to
them fail with `ERROR_INVALID_WINDOW_HANDLE`. The invariant: no queue or window
outlives its thread.

### R09 `WM_QUIT` obeys filters

**Evidence:** chair-verified at
[`:408-434`](../../sta-shim/nanoserver_sta_shim.cpp#L408),
[`:780`](../../sta-shim/nanoserver_sta_shim.cpp#L780), and
[`:840-842`](../../sta-shim/nanoserver_sta_shim.cpp#L840). In-image.

`PostQuitMessage` posts an ordinary thread message at `:841`, and dequeuing
applies both the window filter and the range filter to it at `:418-419`. Its
handle is null, so a pump filtered on a window never sees it and never exits.
A range filter that excludes `WM_QUIT` has the same effect.

**Agreed fix** (Opus's and Sol's, endorsed by Argon): hold quit as per-queue
state with its exit code. Deliver it regardless of filters once no other message
matches, keep it pending under `PM_NOREMOVE`, and keep it behind posted messages
in priority.

### R10 Class registration ignores Win32 rules

**Evidence:** chair-verified in code at
[`:443-479`](../../sta-shim/nanoserver_sta_shim.cpp#L443) and
[`:509-524`](../../sta-shim/nanoserver_sta_shim.cpp#L509). Duplicate
registration and unregistering a missing class are harness (T3, T4). Atom reuse
is code reading only. No container run covered class registration.

Registering a name that already exists overwrites the class and returns its atom
at `:454-464`, where Win32 fails with `ERROR_CLASS_ALREADY_EXISTS`. That silently
replaces another module's window procedure. Classes are keyed by name alone,
with no `hInstance` scoping. `Shim_UnregisterClassW` discards `hInstance` at
`:510`, never checks for live windows, and returns `TRUE` at `:523` even when
nothing matched. Atoms are `0xC000 | (next & 0x3FFF)` at `:467`, from a counter
that starts at 0 (`:153`), so the 16385th registration gets the first one's atom
again and can collide with a live class.

**Agreed fix** (Opus's, with Astra's three identity cases, endorsed by Sol and
Argon; ballot O): scope classes by name and instance unless `CS_GLOBALCLASS`,
resolve local, then global, then system, and return errors 1410, 1411, and 1412
where Win32 does. Refuse to unregister a class that still has windows. Reuse
freed atoms instead of wrapping into live ones. Ship `GetClassInfoW` and
`GetClassInfoExW` in the same change: code that checks before registering starts
failing the moment duplicate registration starts failing correctly.

### R11a `install_features.cmd` cannot fail

**Evidence:** chair-verified at
[`nanoserver-iis-aspnet-x64/install_features.cmd:1-24`](../../nanoserver-iis-aspnet-x64/install_features.cmd#L1),
identical in all four IIS flavours. Shell-verified for the guard cases.

Lines `:4-5` call `:AddCap` for each capability without checking the result, and
line `:7` exits 0 unconditionally. So even a failure that `:AddCap` detects and
returns at `:21` is thrown away. The guards inside `:AddCap` are wrong as well.
`if errorlevel 3010` at `:14` means "3010 or above", so a code such as 3011 is
reported as a reboot request and returns success at `:16`. A negative HRESULT
such as `0x800F081F` (`-2146498529`) fails both `if errorlevel 3010` and
`if errorlevel 1` at `:19`, and falls through to `exit /b 0` at `:24`.

**Agreed fix** (Astra's, endorsed by Argon and Opus): capture `%errorlevel%` into
a variable on the line after DISM, accept exactly 0 and exactly 3010, and return
anything else unchanged. Stop the script on a nonzero return from each
`call :AddCap`. Apply it in all four IIS flavours.

### R11b The .NET Framework images validate their own copy

**Evidence:** chair-verified at
[`nanoserver-iis-netfx-x64/Dockerfile:39-40`](../../nanoserver-iis-netfx-x64/Dockerfile#L39)
against [`:56`](../../nanoserver-iis-netfx-x64/Dockerfile#L56), and
[`nanoserver-iis-netfx-x86/Dockerfile:39-40`](../../nanoserver-iis-netfx-x86/Dockerfile#L39)
against [`:56`](../../nanoserver-iis-netfx-x86/Dockerfile#L56).

Both images copy `SysWOW64/inetsrv/` and `System32/inetsrv/` from the Server
Core installer stage at `:39-40`, then check for `w3wp.exe` at `:56`. The x64
image checks `System32\inetsrv`, copied at `:40`; the x86 image checks
`SysWOW64\inetsrv`, copied at `:39`. The check passes because of the copy, not
because the Nano capability installed, so a missing WOW64 or IIS capability
cannot fail the build here.

**Agreed fix** (Astra's, endorsed by Opus and Argon): check a file only the
capability itself provides, before the `COPY --from=installer` steps, and keep
the copied-file check as a separate later assertion. Opus suggested
`SysWOW64\ntdll.dll` for WOW64 and Nano's own `inetsrv\appcmd.exe` for IIS, and
Argon endorsed them. Which markers are right is still open.

### R12 Test runners treat crash codes as success

**Evidence:** chair-verified at lines `:12`, `:21`, `:30`, and `:39` of all six
`Tests/run_all_modes.cmd` files, for example
[`nanoserver-aspnet-x64/Tests/run_all_modes.cmd:12`](../../nanoserver-aspnet-x64/Tests/run_all_modes.cmd#L12).
Shell-verified: after `exit -1073741819` (an access violation),
`if errorlevel 1` is false.

**Agreed fix** (Sol's, endorsed by Opus and Argon): replace each guard with
`if not "%errorlevel%"=="0" exit /b %errorlevel%` on its own line, in all six
runners, so any nonzero status, negative included, stops the run.

### R14 IIS containers stay alive when IIS fails

**Evidence:** chair-verified, identical in
[`nanoserver-iis-aspnet-x64/Dockerfile:85`](../../nanoserver-iis-aspnet-x64/Dockerfile#L85),
[`nanoserver-iis-aspnet-x86/Dockerfile:87`](../../nanoserver-iis-aspnet-x86/Dockerfile#L87),
[`nanoserver-iis-netfx-x64/Dockerfile:71`](../../nanoserver-iis-netfx-x64/Dockerfile#L71),
and
[`nanoserver-iis-netfx-x86/Dockerfile:71`](../../nanoserver-iis-netfx-x86/Dockerfile#L71):
`CMD ["cmd", "/S", "/C", "net start w3svc >nul 2>&1 & ping -t localhost > nul"]`.

`&` runs `ping` whatever `net start` returned, the output of `net start` goes to
nul, and `ping -t` keeps the container's main process alive forever. A container
whose web server never started looks healthy, and so does one whose service
stopped later.

**Agreed fix** (Astra's, endorsed by Sol, Argon, and Opus): a foreground monitor
that works on Nano Server, reports a failed start with diagnostics, and exits
nonzero when W3SVC or WAS stops. Blocks the four IIS images only.

### R15a The README's security and compatibility claims are false

**Evidence:** chair-verified.

- [`README.md:208`](../../README.md#L208), in the compatibility matrix, says STA
  and Win32 message pumping "Works 100%" on x86 and x64.
- [`README.md:569`](../../README.md#L569) claims W^X compliance and says no
  `PAGE_EXECUTE_READWRITE` memory is ever allocated. Nothing is allocated that
  way, but the hook writes at
  [`nanoserver_sta_shim.cpp:287`](../../sta-shim/nanoserver_sta_shim.cpp#L287)
  and [`:295`](../../sta-shim/nanoserver_sta_shim.cpp#L295) switch existing pages
  to `PAGE_EXECUTE_READWRITE`, which is exactly the state W^X forbids (R17).
- [`README.md:580`](../../README.md#L580) calls STA serialization and message
  pumping "100% Compatible & Faster in Ring 3".

R01 and R03 to R10 contradict `:208` and `:580`. The README also contradicts
itself: `:583` limits synchronous `SendMessageW` to the same thread.

**Agreed fix** (Opus's, endorsed by Astra and Argon): replace the claims at
`:208`, `:569`, and `:580` with one support table, reconciled with `:583`, that
states what the shim implements, what it approximates, and what it does not
implement. List R04 to R10, N01 to N03, the ANSI window and message functions,
and the GDI stubs as known deviations. R01 stands regardless of the
documentation: an unsupported cross-thread send corrupts memory instead of
failing. This gates a release because these claims are what someone would
choose the images on.

### R16 No test asserts the native behaviour behind R01 to R10

**Evidence:** chair-verified unless marked.

The suites do call real `user32` functions. `TestWindow.cs:140-159` imports
window and message functions and `MessagePumpTest.cs:138-156` imports
`PostThreadMessageW`, in all six flavours, under
`Tests/AdaskoTheBeAsT.Interop.COM.Test/`. Those tests assert basic posting and
peeking. That much of Argon's challenge was upheld, and the ballot rejected the
wording "no native contract" (item J). Which implementation answers each call
was not traced. In mode 2 it should be `nanoserver_sta_shim.dll` (inferred from
the installer and resolver code). In mode 1, the managed library initialized at
`TestWindow.cs:64` may intercept the calls first (unverified).

No test covers the native behaviour behind R01 to R10. The
[`NanoServerWindowManagerTest.cs`](../../nanoserver-aspnet-x64/Tests/AdaskoTheBeAsT.Interop.COM.Test/NanoServerWindowManagerTest.cs#L93)
tests assert several of the same deviations as correct, but they test a
different implementation. `NanoServerWindowManager` comes from the
`AdaskoTheBeAsT.Interop.COM` 4.2.0 package
(`AdaskoTheBeAsT.Interop.COM.Test.csproj:16`) and is not defined in this
repository. These tests show that the managed library shares the deviations;
they are not evidence about the native shim.

| Lines in `nanoserver-aspnet-x64` | What the test asserts | Same deviation as |
| --- | --- | --- |
| `:93-95` | Registering a class twice returns the same atom | R10 |
| `:140-141` | Unregistering the same class twice succeeds both times | R10 |
| `:354-370` | A cross-thread send that times out runs the handler directly | R01 |
| `:412-417` | `SetTimer` and `KillTimer` report success | R05 |
| `:425-426` | `MsgWaitForMultipleObjectsEx` returns ready for a zero wake mask and a null handle array | R06 |

All 24 existing container modes passed despite these gaps. That matrix does not
exercise build or installer failure paths, publication tooling, documentation
claims, or the IIS serving entrypoint.

**Agreed fix** (Astra's and Sol's, endorsed by Argon and Opus): native regression
probes through the normal import path, with a Server Core control, covering
thread affinity, lifecycle, timers, wait masks, filters, quit, class identity,
and queue lifetime. Each probe must fail on today's code. This is the part this
repository can fix. Findings outside the shim need their own checks:
fault-injection builds for R02, R11a, and R11b, a deliberately crashing test for
R12, a mocked publish for R13, and a serving check in the IIS images for R14.
Fix the `NanoServerWindowManager` expectations together with a release of the
`AdaskoTheBeAsT.Interop.COM` package that fixes the same deviations. Until then,
mark them as known deviations of the managed library; rewriting them here alone
would only turn the suite red. Scoped as a blocker: probes for R01, R02, and R03
must exist before those fixes count as done.

## P3 findings

### R13 `push.ps1 -Version` throws in a fresh session

Chair-verified: [`push.ps1:12`](../../push.ps1#L12) sets
`Set-StrictMode -Version Latest`. Line `:20` runs `set-version.ps1`, which never
calls `exit` and runs no native command, and line `:21` then reads
`$LASTEXITCODE`. In a fresh session that variable does not exist yet, so strict
mode throws. The check at `:35` is fine: it follows each flavour's `push.ps1`,
which runs `docker` before it returns.

**Fix** (Opus's, endorsed by Argon): delete line 21 and rely on
`$ErrorActionPreference = "Stop"` at `:13`. All four seats settled on P3: it
fails loudly, affects only a maintainer, and happens before anything is pushed.

### R15b Other documentation mismatches

Seat-verified: the descriptions of `Shim_AlwaysTrue`, `SetPropW`, the offline
CAB flow, the `Tests\src` layout, the SRWLOCK use, and the installer's header
comment. How the shim behaves when disabled (`E_NOTIMPL` or otherwise) remains
`unverified`. **Fix:** correct the text.

### R17 Hook writes use executable, writable pages

Chair-verified at
[`:285-299`](../../sta-shim/nanoserver_sta_shim.cpp#L285): both helpers switch
the target to `PAGE_EXECUTE_READWRITE`, and if `VirtualProtect` fails they skip
the write without reporting it. Writers are not serialized either. The
delay-load resolver writes at
[`:1338`](../../sta-shim/nanoserver_sta_shim.cpp#L1338) without taking
`g_HookLock`, while a refresh writes slots at `:1573-1575` under it. Two writers
on the same page can interleave, so the second saves the first one's temporary
protection as the original and restores it last (inferred).

**Fix** (Opus's, endorsed by Argon, with Sol's serialization requirement):
serialize the query, protect, write, and restore sequence across all shim write
paths. Add only write access to the current protection, keep execute permission
only where it was already present, restore the original, and report failures.

### R18 The installer truncates the live DLL before staging

Chair-verified at
[`install_sta_shim.cpp:151-209`](../../sta-shim/install_sta_shim.cpp#L151) and
the identical sequence in `WritePeBufferToFile` at
[`:226-279`](../../sta-shim/install_sta_shim.cpp#L226).

The seed list called the staged copy-and-replace path unreachable. All four
seats agree that is wrong: it runs whenever the direct open fails or the direct
write comes up short. The real defect is the order. The direct open uses
`CREATE_ALWAYS` (`:157`, `:232`), which truncates the live system DLL before a
byte is written. If that write then fails, the original bytes are gone. The
fallback can still put the patched copy in place, but it first moves the
already-truncated file to `.orig` (`:195`, `:268`). If the final move fails, the
rollback (`:201`, `:274`) restores that truncated file, and the caller ignores
the failure (R02). A successful run deletes `.orig` (`:206`, `:278`), so no
backup survives either way.

**Fix** (Astra's, endorsed by Sol, Argon, and Opus): write to a temporary file,
flush and verify it, and only then replace the target, keeping the untouched
original until the replacement is confirmed.

### R19 Hook state is rewritten on every matching query, and the scan is unchecked

Chair-verified. When the shim is enabled and a query names a shimmed ApiSet
namespace, both presence hooks call `RefreshHooksLocked`
([`:1280-1283`](../../sta-shim/nanoserver_sta_shim.cpp#L1280), `:1295-1298`).
Other queries go straight to the original function (`:1289`, `:1307`). Refresh
re-runs discovery for three modules (`:1588-1590`), but discovery returns early
for a module it already captured (`:1455-1459`), so the byte-pattern scan at
`:1541` runs once per module, as Argon argued. What repeats on every matching
query is `ApplyModuleState` (`:1594-1595`, body `:1547-1584`), which rewrites
every hooked slot and cache value through `VirtualProtect` whether or not
anything changed.

Separately, the scan computes addresses from the bytes it matched and never
checks them against the module's image. The string address (`:1395`,
`:1431-1432`) is read through by `IsShimmedUnicodeString` at `:266-267`. The
cache address (`:1397`, `:1434`) is stored at `:1406` and `:1443`, then written
on every refresh at `:1581-1582`, after `VirtualProtect` has made it writable.
After a false pattern match, the shim would write into arbitrary mapped memory
on every matching query (inferred). The multi-byte signature makes a false match
unlikely, so this stays P3.

**Fix** (Sol's, endorsed by Opus and Argon): apply state only when a module is
new or the disable switch changed, skip writes whose value is already in place,
keep reading `NANOSERVER_DISABLE_STA_SHIM` fresh on each query (`:273-283`), and
range-check every derived address before reading or writing through it. The
string must lie inside the module's image, and the cache inside a writable data
section of that module. R25 is merged here.

### R20 `GetUserObjectInformationW(UOI_FLAGS)` lies

Chair-verified at
[`:1005-1016`](../../sta-shim/nanoserver_sta_shim.cpp#L1005): for any index
other than `UOI_NAME`, the function sets the needed length to 0 and returns
`TRUE`, leaving the caller's buffer untouched. **Fix** (Opus's, endorsed by
Argon): check the buffer size, fill `USEROBJECTFLAGS`, report the required
length, and fail unknown indexes. The right `dwFlags` value is open: Opus argues
0 because nothing is visible, Argon argues `WSF_VISIBLE`. .NET's
`Environment.UserInteractive` reads that flag, so the choice is observable.

The earlier allegation that `DispatchMessageW` wrongly invokes TIMERPROC
callbacks is **withdrawn**. Chair-verified at
[`:797-801`](../../sta-shim/nanoserver_sta_shim.cpp#L797): calling the callback
in a `WM_TIMER` message's nonzero `lParam` is documented Win32 behaviour.

### N01 Window filters ignore child windows

Chair-verified at [`:394-402`](../../sta-shim/nanoserver_sta_shim.cpp#L394) and
[`:618-627`](../../sta-shim/nanoserver_sta_shim.cpp#L618). The filter requires
the exact handle, and `IsChild` compares only the direct parent. Win32 delivers
messages for the filtered window **and its children**, and `IsChild` checks the
whole parent chain.

**Fix** (Astra's, endorsed by Argon; Opus proposed the same): one validated walk
up the chain of `WS_CHILD` parents, guarded against cycles, shared by the filter
and `IsChild`, keeping the null and `-1` filter meanings. Severity is P2 for
Argon, Sol, and Astra. Opus holds P3 until a target engine is shown to pump with
a parent-window filter.

### N02 No ANSI entry points

**Severity:** provisional P3 (Argon, Opus). Astra holds it as a P2 candidate that
does not block. Sol would leave it unranked until a probe shows a failure.

Chair-verified: every window and message entry in the export table at
[`:1178-1225`](../../sta-shim/nanoserver_sta_shim.cpp#L1178) is a `...W`
function. The only ANSI entries are `CopyEnhMetaFileA`, `GetTextExtentPoint32A`,
and `GetKeyNameTextA` (`:1217-1222`), which the installer also adds to the
forwarders' export tables
([`install_sta_shim.cpp:649-658`](../../sta-shim/install_sta_shim.cpp#L649)).
That suggests the target engines import ANSI functions (inferred). A missing
table entry does not by itself prove a failure: the delay-load resolver hands
anything it does not resolve to the original resolver (`:1347-1355`). What
`RegisterClassA`, `CreateWindowExA`, or `PeekMessageA` resolve to on Nano Server
is `unverified`.

**Action before escalation or implementation:** run `dumpbin /imports` on
`CoilLib.dll`, `COIL.dll`, and `Selecting.dll`, and `dumpbin /exports` on
Nano's `forwarders\user32.dll`. Trace dynamic `GetProcAddress` calls, which
import tables miss. Then probe the ordinary ANSI entry points on Nano Server and
Server Core. Until then, R15a's support table lists the ANSI window and message
functions as not implemented.

### N03 Missing subclassing and class-query functions

Chair-verified absent from the export table: `GetClassInfoW`,
`GetClassInfoExW`, and `CallWindowProcW`. The first two are required R10 work
and are counted there, not here. That leaves `CallWindowProcW`, which matters
for subclassing chains: add it or list it as a known gap. Severity is P3 for
Argon, Astra, and Opus. Sol would leave it unranked until a normal-import probe
shows a failure. `SendMessageTimeoutW` was dropped from this item: the R01 fix
does not need it.

### N05 Clipboard format ids leave the valid range

Chair-verified at
[`:1030-1047`](../../sta-shim/nanoserver_sta_shim.cpp#L1030) and
[`:156`](../../sta-shim/nanoserver_sta_shim.cpp#L156). The counter starts at
`0xC000` and is incremented before use at `:1043`, so the first id issued is
`0xC001` and `0xC000` is never used. Nothing caps the counter: from the 16384th
distinct registration on, ids are `0x10000` and above, outside the 16-bit range
that registered clipboard formats occupy. The nomination's 14-bit wrap was
wrong; that mask belongs to class atoms (R10), not to this counter.

**Fix** (Argon's; Opus proposed the same): issue `0xC000` to `0xFFFF`, then
return 0 with an error. Argon proposes `ERROR_NOT_ENOUGH_MEMORY`.

## Appendix A: build hardening, outside the defect count

**R21.** No `/guard:cf` in `sta-shim/build_shim.ps1`, no hash verification for
downloaded payloads, and floating base image tags. All four seats agreed this is
hardening policy, not a demonstrated runtime defect (ballot F). Recommended
anyway: a manifest of build inputs with base image digests and payload hashes.

## Appendix B: performance notes, not defects

- **R23.** Image layer depth and `wcifs.sys` lookups were offered as the main
  cause of cold-start overhead. All four seats now treat that as an unproven
  hypothesis: Argon's own table shows an 8-layer Server Core SDK image slower
  than the 9-layer Nano image. Settle it with a flattened-image A/B test before
  changing any Dockerfile.
- **R24.** One process-wide lock (`g_WindowLock`, `:143`) plus a linear queue
  scan (`:301-306`) is real, but its cost is unmeasured. Any per-thread queue
  cache must come after R08 and be cleared by the same thread-exit callback.
- **R26.** Removing `user32`, `ole32`, and `gdi32` from KnownDLLs and routing
  through export forwarders is how the whole design works. Cost unmeasured; not
  a defect.

## Appendix C: withdrawn during the roundtable

| Item | Disposition |
| --- | --- |
| R22, repository hygiene | Withdrawn by its author. At most an unranked note on the test fixtures' debug runtime and manifest metadata. |
| R25 | Merged into R19. |
| N04, `PostMessageW` lock split | Withdrawn by its author. Chair-verified: `:808-828` takes the lock once, across lookup, queueing, and signaling. |
| N06, `set-version.ps1` leaves the README stale | Withdrawn. The README already carries the v1.2.0 tags. |
| Argon round 2: clipboard handle leak, P1 | Withdrawn in round 2b. The shim implements no `OpenClipboard`, `SetClipboardData`, or `EmptyClipboard`. |
| Argon round 2: queue use-after-free on thread detach, P1 | Withdrawn in round 2b. There is no thread-detach code; the real defect is R08's leak. |
| Argon round 2: out-of-memory through extra window bytes, P1 | Folded into R04 at P2 in round 2b. See R04. |
| `DispatchMessageW` TIMERPROC allegation | Withdrawn against the documented contract. See R20. |
| "No test calls native user32" | Withdrawn in Argon's favour. See R16. |
| "The staged fallback is unreachable" | Withdrawn. See R18. |
| "The byte-pattern scan runs on every query" | Withdrawn in Argon's favour. See R19. |

## Surviving disagreements

1. **Do R04 to R10 and N01 block a general release?** Argon, Sol, and Astra:
   yes, because the images advertise general headless STA support. Opus: no, if
   R15a documents them as known deviations.
2. **N01 severity.** P2 (Argon, Sol, Astra) against P3 (Opus). Settled by
   showing whether any target engine pumps with a parent-window filter.
3. **The conditional approval for Argon's workload.** Argon and Opus record it;
   Sol and Astra do not endorse it.
4. **N02 severity.** Provisional P3 (Argon, Opus), P2 candidate (Astra), unranked
   (Sol). Settled by the import inspection, dynamic-call traces, and ANSI probes
   in N02.
5. **N03 ranking.** P3 (Argon, Astra, Opus) against unranked (Sol). Settled by a
   normal-import probe of `CallWindowProcW`.

## Open questions that need a control, not an argument

1. **R03:** does Windows deliver or drop already-posted messages for a destroyed
   window, and does that differ for `WM_TIMER`? Run the same probe on Server
   Core.
2. **R20:** what does `GetUserObjectInformationW(GetProcessWindowStation(),
   UOI_FLAGS, ...)` return on Server Core? Copy its `USEROBJECTFLAGS` exactly.
3. **N02:** which ANSI functions do the three engines import or look up, and
   where do they resolve on Nano Server?
4. **R11b:** which file is provided only by each Nano capability, and is safe to
   check before the copies?
5. **R16:** which implementation answers each test's `user32` calls in modes 1
   and 2? Record the final call targets.
6. **R23:** does a flattened image remove the cold-start overhead?

## Validation and limits

- The chair re-read every line this document cites in the current tree. In
  round 3 all four seats re-read the citations again; the chair verified each
  correction they raised against the code before applying it.
- The chair ran three batch cases in `cmd.exe`: `if errorlevel 1` after exit
  code `-1073741819` is false, `if errorlevel 3010` after 3011 is true, and
  `-2146498529` passes both guards and reaches `exit /b 0`.
- No container was built or run during the roundtable. Container evidence comes
  from Astra's earlier probes on the current `aspnet-x64` and `aspnet-x86`
  images, with a Server Core control, and its 24 of 24 mode runs.
- No seat made a live HTTP request through IIS in any round.
- No performance measurement was repeated. Argon's numbers come from its report
  and were not rerun.
- Harness evidence comes from a program that includes the shim source directly,
  so it skips the import table, the loader hooks, and the installer. Where an
  image probe covered the same case, both are cited.
- No implementation file was modified at any point. The only files written were
  this document, the four droid definitions, and the ignored scratch folder.

## Suggested order of work

1. R01 and R08 together. The send fix and the queue-lifetime fix rest on the
   same shared-ownership change, and neither is safe alone.
2. R02, then R11a and R11b, then R12, each with its fault-injection check.
   Until the build and the test runners can fail, nothing after this is
   measurable.
3. R16's native probes for R01, R02, and R03, written to fail on today's code.
4. R03 with the `DefWindowProcW` corrections, then R05, R06, R07, and R09.
5. R04, R10 with `GetClassInfo(Ex)W`, and N01.
6. R15a, then R14 for the IIS images.
7. The P3 findings, then the appendices.

## Sign-offs

| Seat | Round 3 sign-off | Errors reported | Disposition |
| --- | --- | --- | --- |
| Argon | "I ratify the combined document as written." | 0, plus 3 attribution notes | All 3 notes applied |
| Sol 6.1 | "I ratify the combined document with the exceptions below." | 10 | 9 applied; the N03 ranking objection is recorded as a dissent, since the other three seats rank it P3 |
| Astra | "I ratify the combined document with the exceptions below." | 10 | All 10 applied |
| Opus 5.5 | "I ratify the combined document with the exceptions below." | 12, plus 1 optional addition and 2 attribution notes | All applied |

The 32 reported errors reduce to 23 distinct corrections, plus 5 attribution and
wording notes. None changed the verdict, a severity, or the blocker set; two
recorded existing severity splits that the draft had flattened (N02, N03). Round
3 notes are in `.artifacts/roundtable-2026-10-29/round3/`.

| Finding | Correction applied after round 3 | Raised by |
| --- | --- | --- |
| Verdict | `README.md:583` states the same-thread send limit; the code does not enforce it | Sol |
| Method | Opus split two findings in round 1, not one | Opus |
| R01 | Destruction cancels only requests not yet dispatched; once dispatch starts, the sender stays blocked | Sol, Astra |
| R01 | `SetEvent` on a closed, possibly reused handle | Opus |
| R02 | Preflight only what the selected architectures need; the switch came from Opus and Argon | Sol, Argon |
| R03 | The no-dispatch invariant fails when another thread destroys the window | Sol, Astra, Opus |
| R03 | Parent `WM_DESTROY` comes before child destruction, parent `WM_NCDESTROY` after | Astra |
| R03 | `OleMainThreadWndClass` is registered by combase, not a predefined class | Opus |
| R03, R06, R10 | In-image labels narrowed to what the images tested | Astra, Opus |
| R04 | `bad_alloc` can escape with `g_WindowLock` held | Opus |
| R05 | Removed the unmeasured zero-cost promise | Sol, Astra |
| R06 | With 64 or more handles, input is misreported as the 64th handle | Opus |
| R07 | The 1000 ms figure is harness; in-image is 281 and 265 ms of 300 | Astra, Opus |
| R08 | The current-image measurement is Astra's; Sol's was on v1.1.0 | Sol, Argon |
| R08 | Post and send also create queues; thread exit must remove windows too | Opus |
| R11b | The marker suggestion is Opus's | Argon |
| R15a | Added `README.md:208` and the conflict with `:583` | Opus |
| R16 | The managed tests exercise the external `AdaskoTheBeAsT.Interop.COM` package | Opus |
| R16 | Call routing is inferred for mode 2 and unverified for mode 1 | Astra, Opus |
| R16 | The 24 green modes do not exercise every defect | Astra |
| R17 | Protection changes must be serialized | Sol |
| R19 | Refresh runs only for enabled queries on shimmed namespaces | Sol, Astra |
| R19 | The unchecked cache address is also written through | Opus |
| N02 | Severity split recorded; the R15a action reworded | Sol, Astra, Opus |
| N03 | Sol's ranking dissent recorded | Sol |
| Fix credits | Opus's endorsements added to R06, R07, R08, R11a, R14, R16, R18, N01, N05 | Opus |
