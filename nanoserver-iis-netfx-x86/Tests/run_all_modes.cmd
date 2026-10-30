@echo off
setlocal
cd /d C:\tests

echo ============================================================================
echo Native OS-shim contracts (normal user32 imports, no managed interception)
echo ============================================================================
set NANOSERVER_DISABLE_STA_SHIM=
C:\tests\NativeShimRegression.exe --require-shim
if not "%errorlevel%"=="0" exit /b %errorlevel%

rem Provider switching does not migrate COM's cached class/window state.
rem Keep both state-mutating tests, but give each its own fresh process.
echo ============================================================================
echo [Mode 1/4] Default (Native Shim Available, Library Shim Enabled)
echo ============================================================================
set NANOSERVER_DISABLE_STA_SHIM=
set ADASKO_NANOSERVER_DISABLE_STA_SHIM=
set ADASKO_NANOSERVER_DISABLE_SXS_FALLBACK=
call :RunManagedMode
if not "%errorlevel%"=="0" exit /b %errorlevel%

echo ============================================================================
echo [Mode 2/4] OS-Level STA Shim Only (ADASKO_NANOSERVER_DISABLE_STA_SHIM=1)
echo ============================================================================
set NANOSERVER_DISABLE_STA_SHIM=
set ADASKO_NANOSERVER_DISABLE_STA_SHIM=1
set ADASKO_NANOSERVER_DISABLE_SXS_FALLBACK=
call :RunManagedMode
if not "%errorlevel%"=="0" exit /b %errorlevel%

echo ============================================================================
echo [Mode 3/4] Library In-Process STA Shim Only (NANOSERVER_DISABLE_STA_SHIM=1)
echo ============================================================================
set NANOSERVER_DISABLE_STA_SHIM=1
set ADASKO_NANOSERVER_DISABLE_STA_SHIM=
set ADASKO_NANOSERVER_DISABLE_SXS_FALLBACK=
call :RunManagedMode
if not "%errorlevel%"=="0" exit /b %errorlevel%

echo ============================================================================
echo [Mode 4/4] Both Shims Disabled at Startup + Runtime Toggle Verification
echo ============================================================================
set NANOSERVER_DISABLE_STA_SHIM=1
set ADASKO_NANOSERVER_DISABLE_STA_SHIM=1
set ADASKO_NANOSERVER_DISABLE_SXS_FALLBACK=
call :RunIsolatedToggleTests
if not "%errorlevel%"=="0" exit /b %errorlevel%

echo ============================================================================
echo ALL 4 MODES PASSED SUCCESSFULLY!
echo ============================================================================
exit /b 0

:RunManagedMode
C:\tests\AdaskoTheBeAsT.Interop.COM.Test.exe --filter-not-method "AdaskoTheBeAsT.Interop.COM.Test.NanoServerFallbackTest.NanoServerOptionsShouldManageStaShimStateAndIdentifyShimmedApiSets" --filter-not-method "AdaskoTheBeAsT.Interop.COM.Test.NanoServerWindowManagerTest.NanoServerStaShimCallbacksShouldHandleApiSetPresenceDelayLoadAndModuleHandles"
if not "%errorlevel%"=="0" exit /b %errorlevel%
call :RunIsolatedToggleTests
exit /b %errorlevel%

:RunIsolatedToggleTests
C:\tests\AdaskoTheBeAsT.Interop.COM.Test.exe --filter-method "AdaskoTheBeAsT.Interop.COM.Test.NanoServerFallbackTest.NanoServerOptionsShouldManageStaShimStateAndIdentifyShimmedApiSets"
if not "%errorlevel%"=="0" exit /b %errorlevel%
C:\tests\AdaskoTheBeAsT.Interop.COM.Test.exe --filter-method "AdaskoTheBeAsT.Interop.COM.Test.NanoServerWindowManagerTest.NanoServerStaShimCallbacksShouldHandleApiSetPresenceDelayLoadAndModuleHandles"
if not "%errorlevel%"=="0" exit /b %errorlevel%
exit /b 0
