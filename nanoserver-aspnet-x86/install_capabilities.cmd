@echo off
setlocal

DISM /Online /Add-Capability /CapabilityName:Microsoft.NanoServer.Datacenter.WOWSupport /NoRestart
set "rc=%errorlevel%"

if "%rc%"=="3010" exit /b 0
exit /b %rc%