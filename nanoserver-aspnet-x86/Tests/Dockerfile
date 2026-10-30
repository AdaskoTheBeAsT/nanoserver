# escape=`
ARG BASE_IMAGE=adaskothebeast/nanoserver:aspnet-x86
FROM mcr.microsoft.com/dotnet/sdk:10.0-windowsservercore-ltsc2025 AS build

SHELL ["cmd", "/S", "/C"]
WORKDIR C:\src
COPY global.json Directory.Build.props ./
COPY native ./native/
COPY AdaskoTheBeAsT.Interop.COM.Test ./AdaskoTheBeAsT.Interop.COM.Test/
RUN dotnet build AdaskoTheBeAsT.Interop.COM.Test\AdaskoTheBeAsT.Interop.COM.Test.csproj -c Release -f net10.0 -p:Platform=x86 -o C:\out

FROM ${BASE_IMAGE}

USER ContainerAdministrator
WORKDIR C:\tests
COPY --from=build ["C:/out/", "C:/tests/"]
COPY run_all_modes.cmd C:\tests\run_all_modes.cmd
RUN copy /Y C:\tests\vcruntime*.dll C:\Windows\SysWOW64\ >nul `
    && copy /Y C:\tests\ucrtbased.dll C:\Windows\SysWOW64\ >nul

ENTRYPOINT ["cmd", "/S", "/C", "C:\\tests\\run_all_modes.cmd"]
