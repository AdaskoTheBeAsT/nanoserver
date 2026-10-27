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

ARG DOTNET_VERSION=10.0.14

SHELL ["cmd", "/S", "/C"]

USER ContainerAdministrator

WORKDIR C:\install
COPY install_features.cmd C:\install\install_features.cmd

RUN C:\install\install_features.cmd

# Validate WoW64 and IIS
RUN if exist C:\Windows\SysWOW64 (echo WOW64 installed) else (echo WOW64 missing && exit /b 1)
RUN if exist C:\Windows\System32\inetsrv\appcmd.exe (echo IIS installed) else (echo IIS/appcmd missing && exit /b 1)

# Extract the x86 runtimes directly; Nano Server cannot run the EXE setup engine.
RUN mkdir "C:\Program Files (x86)\dotnet"

RUN curl.exe --fail --location --output dotnet-runtime.zip `
    https://builds.dotnet.microsoft.com/dotnet/Runtime/%DOTNET_VERSION%/dotnet-runtime-%DOTNET_VERSION%-win-x86.zip `
    && tar.exe -xf dotnet-runtime.zip -C "C:\Program Files (x86)\dotnet" `
    && del dotnet-runtime.zip

RUN curl.exe --fail --location --output aspnetcore-runtime.zip `
    https://builds.dotnet.microsoft.com/dotnet/aspnetcore/Runtime/%DOTNET_VERSION%/aspnetcore-runtime-%DOTNET_VERSION%-win-x86.zip `
    && tar.exe -xf aspnetcore-runtime.zip -C "C:\Program Files (x86)\dotnet" `
    && del aspnetcore-runtime.zip

ENV DOTNET_ROOT_X86="C:\Program Files (x86)\dotnet"
RUN setx /M PATH "%PATH%;C:\Program Files (x86)\dotnet"
RUN reg add "HKLM\SOFTWARE\WOW6432Node\dotnet\Setup\InstalledVersions\x86" /v InstallLocation /t REG_SZ /d "C:\Program Files (x86)\dotnet\\" /f

# Verify the x86 host and both shared frameworks.
RUN dotnet --info
RUN dotnet --list-runtimes

# Install ASP.NET Core Module V2 (x64, x86, and IIS schema) and register in IIS
COPY --from=installer ["C:/ancm/x64/", "C:/Program Files/IIS/Asp.Net Core Module/V2/"]
COPY --from=installer ["C:/ancm/x86/", "C:/Program Files (x86)/IIS/Asp.Net Core Module/V2/"]
COPY --from=installer C:/ancm/schema/aspnetcore_schema_v2.xml C:/Windows/System32/inetsrv/config/schema/aspnetcore_schema_v2.xml
COPY --from=installer C:/ancm/tool/ C:/install/ancm-tool/

RUN dotnet C:\install\ancm-tool\RegisterAncm.dll `
    && C:\Windows\System32\inetsrv\appcmd.exe install module /name:AspNetCoreModuleV2 /image:"C:\Program Files\IIS\Asp.Net Core Module\V2\aspnetcorev2.dll" `
    && C:\Windows\System32\inetsrv\appcmd.exe unlock config /section:system.webServer/handlers `
    && rmdir /S /Q C:\install\ancm-tool

# Configure IIS application pools (Nano Server IIS FoD includes 64-bit w3wp.exe, which hosts the 32-bit x86 app out-of-process via ANCM V2)
RUN C:\Windows\System32\inetsrv\appcmd.exe set config -section:applicationPools /applicationPoolDefaults.managedRuntimeVersion:""
RUN C:\Windows\System32\inetsrv\appcmd.exe set apppool /apppool.name:DefaultAppPool /managedRuntimeVersion:""

# Prepare Default Web Site directory for downstream app images
RUN del /Q C:\inetpub\wwwroot\* `
    && mkdir C:\inetpub\wwwroot\logs `
    && icacls C:\inetpub\wwwroot /grant "IIS_IUSRS:(OI)(CI)RX" /T /Q `
    && icacls C:\inetpub\wwwroot\logs /grant "IIS_IUSRS:(OI)(CI)M" /T /Q

# Install OS-level user-mode STA & window/message-queue shim for COM Single-Threaded Apartments (STA)
COPY sta-shim C:\install\sta-shim
RUN C:\install\sta-shim\install_sta_shim.exe C:\install\sta-shim `
    && rmdir /S /Q C:\install\sta-shim

WORKDIR C:\inetpub\wwwroot

EXPOSE 80

CMD ["cmd", "/S", "/C", "net start w3svc >nul 2>&1 & ping -t localhost > nul"]