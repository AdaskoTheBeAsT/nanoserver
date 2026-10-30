@echo off
setlocal

call :AddCap Microsoft.NanoServer.Datacenter.WOWSupport
if not "%errorlevel%"=="0" exit /b %errorlevel%
exit /b 0

:AddCap
echo Installing capability: %~1
DISM /Online /Add-Capability /CapabilityName:%~1 /NoRestart
set "rc=%errorlevel%"
if "%rc%"=="0" exit /b 0
if "%rc%"=="3010" (
    echo Capability %~1 requested reboot; continuing because /NoRestart was used.
    exit /b 0
)
echo Failed installing capability: %~1 [exit code %rc%]
exit /b %rc%