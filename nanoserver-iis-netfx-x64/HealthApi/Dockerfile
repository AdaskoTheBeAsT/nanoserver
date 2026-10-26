# escape=`
FROM mcr.microsoft.com/dotnet/sdk:10.0-windowsservercore-ltsc2025 AS build

SHELL ["cmd", "/S", "/C"]
WORKDIR C:\src
COPY . .
RUN dotnet build HealthApi.csproj -c Release -o C:\out\bin `
    && copy /Y Web.config C:\out\Web.config

FROM adaskothebeast/nanoserver:iis-netfx-x64

COPY --from=build ["C:/out/", "C:/inetpub/wwwroot/"]
RUN icacls C:\inetpub\wwwroot /grant "IIS_IUSRS:(OI)(CI)RX" /T /Q

