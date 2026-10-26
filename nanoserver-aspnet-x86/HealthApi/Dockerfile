# escape=`
FROM mcr.microsoft.com/dotnet/sdk:10.0-windowsservercore-ltsc2025 AS build

SHELL ["cmd", "/S", "/C"]
WORKDIR C:\src
COPY . .
RUN dotnet publish HealthApi.csproj -c Release -r win-x86 --self-contained false -p:UseAppHost=false -o C:\out

FROM adaskothebeast/nanoserver:aspnet-x86

WORKDIR C:\app
COPY --from=build ["C:/out/", "C:/app/"]

USER ContainerUser

ENTRYPOINT ["dotnet", "HealthApi.dll"]
