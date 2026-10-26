# escape=`
FROM mcr.microsoft.com/dotnet/sdk:10.0-windowsservercore-ltsc2025 AS build

SHELL ["cmd", "/S", "/C"]
WORKDIR C:\src
COPY . .
RUN dotnet publish HealthApi.csproj -c Release -r win-x86 --self-contained false -p:UseAppHost=false -o C:\out

FROM adaskothebeast/nanoserver:iis-aspnet-x86

COPY --from=build ["C:/out/", "C:/inetpub/wwwroot/"]
RUN icacls C:\inetpub\wwwroot /grant "IIS_IUSRS:(OI)(CI)RX" /T /Q
