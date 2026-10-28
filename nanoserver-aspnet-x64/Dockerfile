# escape=`
FROM mcr.microsoft.com/windows/nanoserver:ltsc2025

ARG DOTNET_VERSION=10.0.14

SHELL ["cmd", "/S", "/C"]

USER ContainerAdministrator

WORKDIR C:\install

# Extract the x64 runtimes directly; Nano Server cannot run the EXE setup engine.
ENV DOTNET_ROOT="C:\Program Files\dotnet"
RUN mkdir "C:\Program Files\dotnet" `
    && curl.exe --fail --location --output dotnet-runtime.zip `
        https://builds.dotnet.microsoft.com/dotnet/Runtime/%DOTNET_VERSION%/dotnet-runtime-%DOTNET_VERSION%-win-x64.zip `
    && tar.exe -xf dotnet-runtime.zip -C "C:\Program Files\dotnet" `
    && del dotnet-runtime.zip `
    && curl.exe --fail --location --output aspnetcore-runtime.zip `
        https://builds.dotnet.microsoft.com/dotnet/aspnetcore/Runtime/%DOTNET_VERSION%/aspnetcore-runtime-%DOTNET_VERSION%-win-x64.zip `
    && tar.exe -xf aspnetcore-runtime.zip -C "C:\Program Files\dotnet" `
    && del aspnetcore-runtime.zip `
    && setx /M PATH "%PATH%;C:\Program Files\dotnet" `
    && "C:\Program Files\dotnet\dotnet.exe" --info `
    && "C:\Program Files\dotnet\dotnet.exe" --list-runtimes

# Install OS-level user-mode STA & window/message-queue shim for COM Single-Threaded Apartments (STA)
COPY sta-shim C:\install\sta-shim
RUN C:\install\sta-shim\install_sta_shim.exe C:\install\sta-shim `
    && rmdir /S /Q C:\install\sta-shim

WORKDIR C:\app

ENV ASPNETCORE_URLS=http://+:8080
EXPOSE 8080

USER ContainerUser
