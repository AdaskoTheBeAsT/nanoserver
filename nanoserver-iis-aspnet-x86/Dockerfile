# escape=`
ARG DOTNET_VERSION=10.0.14

FROM mcr.microsoft.com/dotnet/sdk:10.0-windowsservercore-ltsc2025 AS installer

ARG DOTNET_VERSION=10.0.14

SHELL ["cmd", "/S", "/C"]
WORKDIR C:\src
COPY RegisterAncm.cs ./

RUN dotnet publish RegisterAncm.cs -c Release --self-contained false -p:UseAppHost=false -o C:\ancm\tool

# Extract ASP.NET Core Module V2 in the SDK stage; Nano Server cannot run msiexec.
RUN curl.exe --fail --location --output C:\ancm.msi `
    https://builds.dotnet.microsoft.com/dotnet/aspnetcore/Runtime/%DOTNET_VERSION%/aspnetcoremodule_x64_en_v2_%DOTNET_VERSION%.msi `
    && start /wait msiexec.exe /a C:\ancm.msi /qn TARGETDIR=C:\ancm_extract `
    && mkdir C:\ancm\x64 C:\ancm\x86 C:\ancm\schema `
    && xcopy /E /I /Y "C:\ancm_extract\PFiles64\IIS\Asp.Net Core Module\V2" C:\ancm\x64 `
    && copy /Y "C:\ancm_extract\PFiles\IIS\Asp.Net Core Module\WowOnly\aspnetcorev2.dll" C:\ancm\x86\aspnetcorev2.dll `
    && (for /D %D in ("C:\ancm_extract\PFiles64\IIS\Asp.Net Core Module\V2\*") do mkdir "C:\ancm\x86\%~nxD" `
    && copy /Y "C:\ancm_extract\PFiles\IIS\Asp.Net Core Module\WowOnly\WowOnly\aspnetcorev2_outofprocess.dll" "C:\ancm\x86\%~nxD\aspnetcorev2_outofprocess.dll") `
    && copy /Y "C:\ancm_extract\System64\inetsrv\config\schema\aspnetcore_schema_v2.xml" C:\ancm\schema\aspnetcore_schema_v2.xml

FROM mcr.microsoft.com/windows/nanoserver:ltsc2025

SHELL ["cmd", "/S", "/C"]

USER ContainerAdministrator

WORKDIR C:\install
COPY install_features.cmd C:\install\install_features.cmd

RUN C:\install\install_features.cmd

# Complete post-reboot CBS servicing (ResolvePendingTransactions) while Winmgmt is running, then validate WoW64 and IIS
RUN DISM /Online /Get-Capabilities >nul `
    && (net stop TrustedInstaller >nul 2>&1 & del /F /Q C:\Windows\Logs\CBS\*.log C:\Windows\Logs\DISM\*.log >nul 2>&1) `
    && (if exist C:\Windows\WinSxS\pending.xml (echo CBS pending.xml still present && exit /b 1)) `
    && (if exist C:\Windows\SysWOW64\ntdll.dll (echo WOW64 installed) else (echo WOW64 missing && exit /b 1)) `
    && (if exist C:\Windows\System32\inetsrv\appcmd.exe (echo IIS installed) else (echo IIS/appcmd missing && exit /b 1))

ARG DOTNET_VERSION=10.0.14

# Extract the x86 runtimes directly; Nano Server cannot run the EXE setup engine.
ENV DOTNET_ROOT_X86="C:\Program Files (x86)\dotnet"
RUN mkdir "C:\Program Files (x86)\dotnet" `
    && curl.exe --fail --location --output dotnet-runtime.zip `
        https://builds.dotnet.microsoft.com/dotnet/Runtime/%DOTNET_VERSION%/dotnet-runtime-%DOTNET_VERSION%-win-x86.zip `
    && tar.exe -xf dotnet-runtime.zip -C "C:\Program Files (x86)\dotnet" `
    && del dotnet-runtime.zip `
    && curl.exe --fail --location --output aspnetcore-runtime.zip `
        https://builds.dotnet.microsoft.com/dotnet/aspnetcore/Runtime/%DOTNET_VERSION%/aspnetcore-runtime-%DOTNET_VERSION%-win-x86.zip `
    && tar.exe -xf aspnetcore-runtime.zip -C "C:\Program Files (x86)\dotnet" `
    && del aspnetcore-runtime.zip `
    && setx /M PATH "%PATH%;C:\Program Files (x86)\dotnet" `
    && reg add "HKLM\SOFTWARE\WOW6432Node\dotnet\Setup\InstalledVersions\x86" /v InstallLocation /t REG_SZ /d "C:\Program Files (x86)\dotnet\\" /f `
    && "C:\Program Files (x86)\dotnet\dotnet.exe" --info `
    && "C:\Program Files (x86)\dotnet\dotnet.exe" --list-runtimes

# Install ASP.NET Core Module V2 (x64, x86, and IIS schema), register in IIS, and configure DefaultAppPool
COPY --from=installer ["C:/ancm/x64/", "C:/Program Files/IIS/Asp.Net Core Module/V2/"]
COPY --from=installer ["C:/ancm/x86/", "C:/Program Files (x86)/IIS/Asp.Net Core Module/V2/"]
COPY --from=installer C:/ancm/schema/aspnetcore_schema_v2.xml C:/Windows/System32/inetsrv/config/schema/aspnetcore_schema_v2.xml
COPY --from=installer C:/ancm/tool/ C:/install/ancm-tool/

RUN dotnet C:\install\ancm-tool\RegisterAncm.dll `
    && C:\Windows\System32\inetsrv\appcmd.exe install module /name:AspNetCoreModuleV2 /image:"C:\Program Files\IIS\Asp.Net Core Module\V2\aspnetcorev2.dll" `
    && C:\Windows\System32\inetsrv\appcmd.exe unlock config /section:system.webServer/handlers `
    && rmdir /S /Q C:\install\ancm-tool `
    && C:\Windows\System32\inetsrv\appcmd.exe set config -section:applicationPools /applicationPoolDefaults.managedRuntimeVersion:"" `
    && C:\Windows\System32\inetsrv\appcmd.exe set apppool /apppool.name:DefaultAppPool /managedRuntimeVersion:"" `
    && del /Q C:\inetpub\wwwroot\* `
    && mkdir C:\inetpub\wwwroot\logs `
    && icacls C:\inetpub\wwwroot /grant "IIS_IUSRS:(OI)(CI)RX" /T /Q `
    && icacls C:\inetpub\wwwroot\logs /grant "IIS_IUSRS:(OI)(CI)M" /T /Q

# Install OS-level user-mode STA & window/message-queue shim for COM Single-Threaded Apartments (STA)
COPY sta-shim C:\install\sta-shim
RUN C:\install\sta-shim\install_sta_shim.exe C:\install\sta-shim --require-wow64 `
    && copy /Y C:\install\sta-shim\iis_service_monitor.exe C:\iis_service_monitor.exe `
    && rmdir /S /Q C:\install\sta-shim

WORKDIR C:\inetpub\wwwroot

EXPOSE 80

CMD ["C:\\iis_service_monitor.exe"]