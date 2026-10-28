@echo off
setlocal
cd /d C:\tests

echo ============================================================================
echo [Mode 1/4] Default (OS-Level STA Shim + Library In-Process STA Shim Active)
echo ============================================================================
set NANOSERVER_DISABLE_STA_SHIM=
set ADASKO_NANOSERVER_DISABLE_STA_SHIM=
set ADASKO_NANOSERVER_DISABLE_SXS_FALLBACK=
C:\tests\AdaskoTheBeAsT.Interop.COM.Test.exe
if errorlevel 1 exit /b %errorlevel%

echo ============================================================================
echo [Mode 2/4] OS-Level STA Shim Only (ADASKO_NANOSERVER_DISABLE_STA_SHIM=1)
echo ============================================================================
set NANOSERVER_DISABLE_STA_SHIM=
set ADASKO_NANOSERVER_DISABLE_STA_SHIM=1
set ADASKO_NANOSERVER_DISABLE_SXS_FALLBACK=
C:\tests\AdaskoTheBeAsT.Interop.COM.Test.exe
if errorlevel 1 exit /b %errorlevel%

echo ============================================================================
echo [Mode 3/4] Library In-Process STA Shim Only (NANOSERVER_DISABLE_STA_SHIM=1)
echo ============================================================================
set NANOSERVER_DISABLE_STA_SHIM=1
set ADASKO_NANOSERVER_DISABLE_STA_SHIM=
set ADASKO_NANOSERVER_DISABLE_SXS_FALLBACK=
C:\tests\AdaskoTheBeAsT.Interop.COM.Test.exe
if errorlevel 1 exit /b %errorlevel%

echo ============================================================================
echo [Mode 4/4] Both Shims Disabled at Startup + Runtime Toggle Verification
echo ============================================================================
set NANOSERVER_DISABLE_STA_SHIM=1
set ADASKO_NANOSERVER_DISABLE_STA_SHIM=1
set ADASKO_NANOSERVER_DISABLE_SXS_FALLBACK=
C:\tests\AdaskoTheBeAsT.Interop.COM.Test.exe --filter-method "AdaskoTheBeAsT.Interop.COM.Test.NanoServerFallbackTest.NanoServerOptionsShouldManageStaShimStateAndIdentifyShimmedApiSets"
if errorlevel 1 exit /b %errorlevel%

echo ============================================================================
echo ALL 4 MODES PASSED SUCCESSFULLY!
echo ============================================================================
exit /b 0
