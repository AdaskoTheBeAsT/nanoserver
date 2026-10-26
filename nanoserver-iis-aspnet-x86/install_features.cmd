@echo off
setlocal

call :AddCap Microsoft.NanoServer.IIS
call :AddCap Microsoft.NanoServer.Datacenter.WOWSupport

exit /b 0

:AddCap
echo Installing capability: %~1
DISM /Online /Add-Capability /CapabilityName:%~1 /NoRestart

REM Some Nano FoDs request reboot and return 3010; treat that as success.
if errorlevel 3010 (
    echo Capability %~1 requested reboot; continuing because /NoRestart was used.
    exit /b 0
)

if errorlevel 1 (
    echo Failed installing capability: %~1
    exit /b %errorlevel%
)

exit /b 0