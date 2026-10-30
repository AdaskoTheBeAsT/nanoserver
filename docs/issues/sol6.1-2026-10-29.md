# Verdict: not production-ready yet

Review date: 2026-10-29.

This document preserves the review of commit `d359366` (version `1.1.0`), before
the subsequent `1.2.0` changes. Findings and line references describe that review
snapshot, not a new assessment of the current working tree.

I found **10 actionable issues**. The existing test suite passes, but additional
native probes expose failures.

## High priority

- **[P1] Preserve thread affinity in `SendMessageW`**
  [`nanoserver_sta_shim.cpp:898`](../../sta-shim/nanoserver_sta_shim.cpp#L898):
  After a timeout, the shim runs the window procedure on the sender instead of
  its owning thread. Reproduced on x86 and x64. If the receiver has already
  started processing, returning also leaves it referencing expired stack state.

- **[P1] Abort installation when copying the shim fails**
  [`install_sta_shim.cpp:261`](../../sta-shim/install_sta_shim.cpp#L261):
  Missing payload files are silently ignored. The installer still patches system
  DLLs to require the missing shim and returns success. Reproduced in a disposable
  container.

- **[P1] Deliver window creation messages**
  [`nanoserver_sta_shim.cpp:588`](../../sta-shim/nanoserver_sta_shim.cpp#L588):
  `CreateWindowExW` ignores `lpParam` and never delivers `WM_NCCREATE` or
  `WM_CREATE`. Window procedures that initialize their state there remain
  uninitialized.

## Other confirmed bugs

- **[P2] Implement timer scheduling**,
  [`shim:930`](../../sta-shim/nanoserver_sta_shim.cpp#L930).
  `SetTimer` returns success but never produces timer messages or callbacks.
- **[P2] Honor wait masks and `MWMO_WAITALL`**,
  [`shim:962`](../../sta-shim/nanoserver_sta_shim.cpp#L962).
  Unrelated queued messages incorrectly complete waits, even with unsignaled
  handles.
- **[P2] Write only four bytes for `SetWindowLongW`**,
  [`shim:727`](../../sta-shim/nanoserver_sta_shim.cpp#L727).
  On x64, it writes eight bytes and corrupts adjacent window data.
- **[P2] Release retired thread queues**,
  [`shim:307`](../../sta-shim/nanoserver_sta_shim.cpp#L307).
  Creating and retiring 64 message-pump threads leaked 64 event handles on both
  architectures.
- **[P2] Retrieve `WM_QUIT` regardless of filters**,
  [`shim:418`](../../sta-shim/nanoserver_sta_shim.cpp#L418).
  Filtering it out can prevent message loops from terminating.
- **[P2] Remove the undefined `$LASTEXITCODE` check**,
  [`push.ps1:21`](../../push.ps1#L21).
  `push.ps1 -Version ...` fails in a fresh PowerShell session.
- **[P2] Detect negative crash exit codes**,
  [`run_all_modes.cmd:12`](../../nanoserver-aspnet-x64/Tests/run_all_modes.cmd#L12).
  All six runners use `if errorlevel 1`, which misses native crash codes and can
  falsely report success.

## Validation

- All **24 existing container test runs passed**.
- All **47 PowerShell files parsed successfully**.
- Native probes reproduced the API failures above; the Server Core control
  behaved correctly.
- `git diff --check` reported whitespace errors.
- No clean image rebuilds were performed.
- No tracked files were changed during the review.
