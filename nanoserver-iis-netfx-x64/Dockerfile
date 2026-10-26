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

# Copy .NET Framework 4.8.1 (x86 + x64 + GAC), CLR CRT libraries, and 32-bit/64-bit IIS ASP.NET binaries
# while keeping Nano Server's native C:\Windows\SysWOW64\forwarders and System32\forwarders intact.
COPY --from=installer ["C:/Windows/Microsoft.NET/", "C:/Windows/Microsoft.NET/"]
COPY --from=installer ["C:/Windows/SysWOW64/mscoree.dll", "C:/Windows/SysWOW64/ucrtbase_clr0400.dll", "C:/Windows/SysWOW64/vcruntime140_clr0400.dll", "C:/Windows/SysWOW64/"]
COPY --from=installer ["C:/Windows/System32/mscoree.dll", "C:/Windows/System32/ucrtbase_clr0400.dll", "C:/Windows/System32/vcruntime140_clr0400.dll", "C:/Windows/System32/vcruntime140_1_clr0400.dll", "C:/Windows/System32/"]
COPY --from=installer ["C:/Windows/SysWOW64/inetsrv/", "C:/Windows/SysWOW64/inetsrv/"]
COPY --from=installer ["C:/Windows/System32/inetsrv/", "C:/Windows/System32/inetsrv/"]
COPY --from=installer ["C:/reg/", "C:/install/reg/"]

# Import .NET Framework 4.8.1 and ASP.NET registry hives
# and configure DefaultAppPool for 64-bit (x64) .NET Framework 4.0 Integrated mode.
RUN reg import C:\install\reg\ndp64.reg `
    && reg import C:\install\reg\ndp32.reg `
    && reg import C:\install\reg\netfx64.reg `
    && reg import C:\install\reg\netfx32.reg `
    && reg import C:\install\reg\aspnet64.reg `
    && reg import C:\install\reg\aspnet32.reg `
    && reg import C:\install\reg\aspnetsvc.reg `
    && reg copy "HKLM\SOFTWARE\Microsoft\InetStp" "HKLM\SOFTWARE\WOW6432Node\Microsoft\InetStp" /s /f `
    && rmdir /S /Q C:\install\reg `
    && C:\Windows\System32\inetsrv\appcmd.exe set config -section:applicationPools /applicationPoolDefaults.enable32BitAppOnWin64:false /applicationPoolDefaults.managedRuntimeVersion:v4.0 /applicationPoolDefaults.managedPipelineMode:Integrated `
    && C:\Windows\System32\inetsrv\appcmd.exe set apppool /apppool.name:DefaultAppPool /enable32BitAppOnWin64:false /managedRuntimeVersion:v4.0 /managedPipelineMode:Integrated

# Validate 64-bit IIS worker process and 64-bit .NET Framework 4.8.1 CLR
RUN if exist C:\Windows\System32\inetsrv\w3wp.exe (echo 64-bit IIS worker present) else (echo 64-bit w3wp missing && exit /b 1)
RUN if exist C:\Windows\Microsoft.NET\Framework64\v4.0.30319\clr.dll (echo 64-bit .NET Framework 4.8.1 present) else (echo .NET Framework 4.8.1 missing && exit /b 1)
RUN reg query "HKLM\SOFTWARE\Microsoft\NET Framework Setup\NDP\v4\Full" /v Release

# Prepare Default Web Site directory for downstream app images
RUN del /Q C:\inetpub\wwwroot\* `
    && icacls C:\inetpub\wwwroot /grant "IIS_IUSRS:(OI)(CI)RX" /T /Q

WORKDIR C:\inetpub\wwwroot

EXPOSE 80

CMD ["cmd", "/S", "/C", "net start w3svc >nul 2>&1 & ping -t localhost > nul"]

