# escape=`
FROM mcr.microsoft.com/windows/nanoserver:ltsc2025

ARG DOTNET_VERSION=10.0.14

SHELL ["cmd", "/S", "/C"]

USER ContainerAdministrator

WORKDIR C:\install
COPY install_capabilities.cmd .


RUN C:\install\install_capabilities.cmd

# Complete post-reboot CBS servicing (ResolvePendingTransactions) while Winmgmt is running, then validate WoW64
RUN DISM /Online /Get-Capabilities >nul `
    && (net stop TrustedInstaller >nul 2>&1 & del /F /Q C:\Windows\Logs\CBS\*.log C:\Windows\Logs\DISM\*.log >nul 2>&1) `
    && (if exist C:\Windows\WinSxS\pending.xml (echo CBS pending.xml still present && exit /b 1)) `
    && (if exist C:\Windows\SysWOW64\ntdll.dll (echo WOW64 installed) else (echo WOW64 missing && exit /b 1))

# Extract the runtimes directly; Nano Server cannot run the EXE setup engine.
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
    && "C:\Program Files (x86)\dotnet\dotnet.exe" --info `
    && "C:\Program Files (x86)\dotnet\dotnet.exe" --list-runtimes

# Install OS-level user-mode STA & window/message-queue shim for COM Single-Threaded Apartments (STA)
COPY sta-shim C:\install\sta-shim
RUN C:\install\sta-shim\install_sta_shim.exe C:\install\sta-shim --require-wow64 `
    && rmdir /S /Q C:\install\sta-shim

WORKDIR C:\app

ENV ASPNETCORE_URLS=http://+:8080
EXPOSE 8080

USER ContainerUser

