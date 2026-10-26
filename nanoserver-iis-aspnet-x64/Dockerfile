# escape=`
ARG DOTNET_VERSION=10.0.13

FROM mcr.microsoft.com/dotnet/sdk:10.0-windowsservercore-ltsc2025 AS installer

ARG DOTNET_VERSION=10.0.13

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

ARG DOTNET_VERSION=10.0.13

SHELL ["cmd", "/S", "/C"]

USER ContainerAdministrator

WORKDIR C:\install
COPY install_features.cmd C:\install\install_features.cmd

RUN C:\install\install_features.cmd

# Validate IIS
RUN if exist C:\Windows\System32\inetsrv\appcmd.exe (echo IIS installed) else (echo IIS/appcmd missing && exit /b 1)

# Extract the x64 runtimes directly; Nano Server cannot run the EXE setup engine.
RUN mkdir "C:\Program Files\dotnet"

RUN curl.exe --fail --location --output dotnet-runtime.zip `
    https://builds.dotnet.microsoft.com/dotnet/Runtime/%DOTNET_VERSION%/dotnet-runtime-%DOTNET_VERSION%-win-x64.zip `
    && tar.exe -xf dotnet-runtime.zip -C "C:\Program Files\dotnet" `
    && del dotnet-runtime.zip

RUN curl.exe --fail --location --output aspnetcore-runtime.zip `
    https://builds.dotnet.microsoft.com/dotnet/aspnetcore/Runtime/%DOTNET_VERSION%/aspnetcore-runtime-%DOTNET_VERSION%-win-x64.zip `
    && tar.exe -xf aspnetcore-runtime.zip -C "C:\Program Files\dotnet" `
    && del aspnetcore-runtime.zip

ENV DOTNET_ROOT="C:\Program Files\dotnet"
RUN setx /M PATH "%PATH%;C:\Program Files\dotnet"
RUN reg add "HKLM\SOFTWARE\dotnet\Setup\InstalledVersions\x64" /v InstallLocation /t REG_SZ /d "C:\Program Files\dotnet\\" /f

# Verify the x64 host and both shared frameworks.
RUN dotnet --info
RUN dotnet --list-runtimes

# Install ASP.NET Core Module V2 (x64 and IIS schema) and register in IIS
COPY --from=installer ["C:/ancm/x64/", "C:/Program Files/IIS/Asp.Net Core Module/V2/"]
COPY --from=installer C:/ancm/schema/aspnetcore_schema_v2.xml C:/Windows/System32/inetsrv/config/schema/aspnetcore_schema_v2.xml
COPY --from=installer C:/ancm/tool/ C:/install/ancm-tool/

RUN dotnet C:\install\ancm-tool\RegisterAncm.dll `
    && C:\Windows\System32\inetsrv\appcmd.exe install module /name:AspNetCoreModuleV2 /image:"C:\Program Files\IIS\Asp.Net Core Module\V2\aspnetcorev2.dll" `
    && C:\Windows\System32\inetsrv\appcmd.exe unlock config /section:system.webServer/handlers `
    && rmdir /S /Q C:\install\ancm-tool

# Configure IIS application pools for 64-bit ASP.NET Core
RUN C:\Windows\System32\inetsrv\appcmd.exe set config -section:applicationPools /applicationPoolDefaults.enable32BitAppOnWin64:false /applicationPoolDefaults.managedRuntimeVersion:""
RUN C:\Windows\System32\inetsrv\appcmd.exe set apppool /apppool.name:DefaultAppPool /enable32BitAppOnWin64:false /managedRuntimeVersion:""

# Prepare Default Web Site directory for downstream app images
RUN del /Q C:\inetpub\wwwroot\* `
    && mkdir C:\inetpub\wwwroot\logs `
    && icacls C:\inetpub\wwwroot /grant "IIS_IUSRS:(OI)(CI)RX" /T /Q `
    && icacls C:\inetpub\wwwroot\logs /grant "IIS_IUSRS:(OI)(CI)M" /T /Q

WORKDIR C:\inetpub\wwwroot

EXPOSE 80

CMD ["cmd", "/S", "/C", "net start w3svc >nul 2>&1 & ping -t localhost > nul"]