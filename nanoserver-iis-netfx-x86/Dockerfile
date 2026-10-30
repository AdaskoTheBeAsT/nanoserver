# escape=`
FROM mcr.microsoft.com/dotnet/sdk:10.0-windowsservercore-ltsc2025 AS installer

SHELL ["cmd", "/S", "/C"]

# Enable IIS + ASP.NET 4.8.1 in the Server Core installer stage to harvest 32-bit/64-bit inetsrv (w3wp.exe),
# ASP.NET 4.8.1 IIS configuration, and .NET Framework 4.8.1 registry keys for Nano Server.
RUN DISM /Online /Enable-Feature /All /FeatureName:IIS-WebServerRole /FeatureName:IIS-ASPNET45 /NoRestart `
    && mkdir C:\reg `
    && reg export "HKLM\SOFTWARE\Microsoft\NET Framework Setup" C:\reg\ndp64.reg /y `
    && reg export "HKLM\SOFTWARE\WOW6432Node\Microsoft\NET Framework Setup" C:\reg\ndp32.reg /y `
    && reg export "HKLM\SOFTWARE\Microsoft\.NETFramework" C:\reg\netfx64.reg /y `
    && reg export "HKLM\SOFTWARE\WOW6432Node\Microsoft\.NETFramework" C:\reg\netfx32.reg /y `
    && reg export "HKLM\SOFTWARE\Microsoft\ASP.NET" C:\reg\aspnet64.reg /y `
    && reg export "HKLM\SOFTWARE\WOW6432Node\Microsoft\ASP.NET" C:\reg\aspnet32.reg /y `
    && reg export "HKLM\SYSTEM\CurrentControlSet\Services\ASP.NET_4.0.30319" C:\reg\aspnetsvc.reg /y

FROM mcr.microsoft.com/windows/nanoserver:ltsc2025

SHELL ["cmd", "/S", "/C"]

USER ContainerAdministrator

WORKDIR C:\install
COPY install_features.cmd C:\install\install_features.cmd

RUN C:\install\install_features.cmd

# Complete post-reboot CBS servicing (ResolvePendingTransactions) while Winmgmt is running
RUN DISM /Online /Get-Capabilities >nul `
    && (net stop TrustedInstaller >nul 2>&1 & del /F /Q C:\Windows\Logs\CBS\*.log C:\Windows\Logs\DISM\*.log >nul 2>&1) `
    && (if exist C:\Windows\WinSxS\pending.xml (echo CBS pending.xml still present && exit /b 1)) `
    && (if exist C:\Windows\SysWOW64\ntdll.dll (echo WOW64 installed) else (echo WOW64 missing && exit /b 1)) `
    && (if exist C:\Windows\System32\inetsrv\appcmd.exe (echo IIS installed) else (echo IIS/appcmd missing && exit /b 1))

# Copy .NET Framework 4.8.1 (x86 + x64 + GAC), CLR CRT libraries, and 32-bit/64-bit IIS ASP.NET binaries
# while keeping Nano Server's native C:\Windows\SysWOW64\forwarders and System32\forwarders intact.
COPY --from=installer ["C:/Windows/Microsoft.NET/", "C:/Windows/Microsoft.NET/"]
COPY --from=installer ["C:/Windows/SysWOW64/mscoree.dll", "C:/Windows/SysWOW64/ucrtbase_clr0400.dll", "C:/Windows/SysWOW64/vcruntime140_clr0400.dll", "C:/Windows/SysWOW64/"]
COPY --from=installer ["C:/Windows/System32/mscoree.dll", "C:/Windows/System32/ucrtbase_clr0400.dll", "C:/Windows/System32/vcruntime140_clr0400.dll", "C:/Windows/System32/vcruntime140_1_clr0400.dll", "C:/Windows/System32/"]
COPY --from=installer ["C:/Windows/SysWOW64/inetsrv/", "C:/Windows/SysWOW64/inetsrv/"]
RUN move C:\Windows\System32\inetsrv\config\schema C:\Windows\Temp\iis_schema_old >nul `
    && move C:\Windows\System32\inetsrv\en-US C:\Windows\Temp\iis_enus_old >nul
COPY --from=installer ["C:/Windows/System32/inetsrv/", "C:/Windows/System32/inetsrv/"]
COPY --from=installer ["C:/reg/", "C:/install/reg/"]

# Import .NET Framework 4.8.1 and ASP.NET registry hives, mirror InetStp to WOW6432Node for 32-bit System.Web,
# configure DefaultAppPool for 32-bit (x86) .NET Framework 4.0 Integrated mode, and validate installation.
RUN reg import C:\install\reg\ndp64.reg `
    && reg import C:\install\reg\ndp32.reg `
    && reg import C:\install\reg\netfx64.reg `
    && reg import C:\install\reg\netfx32.reg `
    && reg import C:\install\reg\aspnet64.reg `
    && reg import C:\install\reg\aspnet32.reg `
    && reg import C:\install\reg\aspnetsvc.reg `
    && reg copy "HKLM\SOFTWARE\Microsoft\InetStp" "HKLM\SOFTWARE\WOW6432Node\Microsoft\InetStp" /s /f `
    && rmdir /S /Q C:\install\reg `
    && C:\Windows\System32\inetsrv\appcmd.exe set config -section:applicationPools /applicationPoolDefaults.enable32BitAppOnWin64:true /applicationPoolDefaults.managedRuntimeVersion:v4.0 /applicationPoolDefaults.managedPipelineMode:Integrated `
    && C:\Windows\System32\inetsrv\appcmd.exe set apppool /apppool.name:DefaultAppPool /enable32BitAppOnWin64:true /managedRuntimeVersion:v4.0 /managedPipelineMode:Integrated `
    && (if exist C:\Windows\SysWOW64\inetsrv\w3wp.exe (echo 32-bit IIS worker present) else (echo 32-bit w3wp missing && exit /b 1)) `
    && (if exist C:\Windows\Microsoft.NET\Framework\v4.0.30319\clr.dll (echo 32-bit .NET Framework 4.8.1 present) else (echo .NET Framework 4.8.1 missing && exit /b 1)) `
    && reg query "HKLM\SOFTWARE\Microsoft\NET Framework Setup\NDP\v4\Full" /v Release `
    && del /Q C:\inetpub\wwwroot\* `
    && icacls C:\Windows\System32\inetsrv\config\schema /grant "*S-1-5-32-545:(OI)(CI)RX" "*S-1-15-2-1:(OI)(CI)RX" "*S-1-15-2-2:(OI)(CI)RX" /Q `
    && icacls C:\inetpub\wwwroot /grant "IIS_IUSRS:(OI)(CI)RX" /T /Q

# Install OS-level user-mode STA & window/message-queue shim for COM Single-Threaded Apartments (STA)
COPY sta-shim C:\install\sta-shim
RUN C:\install\sta-shim\install_sta_shim.exe C:\install\sta-shim --require-wow64 `
    && copy /Y C:\install\sta-shim\iis_service_monitor.exe C:\iis_service_monitor.exe `
    && rmdir /S /Q C:\install\sta-shim

WORKDIR C:\inetpub\wwwroot

EXPOSE 80

CMD ["C:\\iis_service_monitor.exe"]

