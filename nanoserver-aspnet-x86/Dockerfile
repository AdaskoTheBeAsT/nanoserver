# escape=`
FROM mcr.microsoft.com/windows/nanoserver:ltsc2025

ARG DOTNET_VERSION=10.0.13

SHELL ["cmd", "/S", "/C"]

USER ContainerAdministrator

WORKDIR C:\install
COPY install_capabilities.cmd .


RUN C:\install\install_capabilities.cmd
RUN if exist C:\Windows\SysWOW64 (echo WOW64 installed) else (echo WOW64 missing && exit /b 1)

# Extract the runtimes directly; Nano Server cannot run the EXE setup engine.
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

# Verify the x86 host and both shared frameworks.
RUN dotnet --info
RUN dotnet --list-runtimes

WORKDIR C:\app

ENV ASPNETCORE_URLS=http://+:8080
EXPOSE 8080

USER ContainerUser

