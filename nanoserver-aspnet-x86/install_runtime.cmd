@echo off
setlocal

REM Wait for the installer to finish and capture its exit code.
start "" /wait "%~1" /install /quiet /norestart /log "C:\install\%~2.log"
set "rc=%errorlevel%"

if "%rc%"=="0" exit /b 0
if "%rc%"=="3010" exit /b 0

echo Installer %~1 failed with exit code %rc%.

REM Print the bundle log and any child installer logs.
for %%F in ("C:\install\%~2*.log") do (
    echo ==== %%F ====
    type "%%F"
)

exit /b %rc%