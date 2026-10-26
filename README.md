# Windows Server 2025 containers

This repository describes the supported Windows Server 2025 container targets
for IIS and .NET workloads. The workloads do not all run on Nano Server:

| Workload | Base image | Architecture |
| --- | --- | --- |
| IIS 10 with ASP.NET Core 10 | `mcr.microsoft.com/windows/nanoserver:ltsc2025` | x64 |
| IIS 10 with .NET Framework 4.8.1 | `mcr.microsoft.com/windows/servercore:ltsc2025` | x86 and x64 |

Nano Server is a 64-bit-only image and does not include the full .NET Framework.
Therefore, .NET Framework 4.8.1 applications—including 32-bit applications—
must use Server Core, not Nano Server. ASP.NET Core applications can target
Nano Server as 64-bit applications. IIS on Nano Server is limited to the
features and modules available for that image; verify that the required
ASP.NET Core hosting components are supported before deployment.

For more information, see Microsoft's documentation:

- [Windows container base images](https://learn.microsoft.com/en-us/virtualization/windowscontainers/manage-containers/container-base-images)
- [Introducing IIS on Nano Server](https://learn.microsoft.com/en-us/iis/get-started/whats-new-in-iis-10/introducing-iis-on-nano-server)
- [Running ASP.NET Core with IIS on Nano Server](https://learn.microsoft.com/en-us/iis/get-started/whats-new-in-iis-10/running-aspnet-core-with-iis-on-nano-server)
- [.NET Framework container scenarios](https://learn.microsoft.com/en-us/dotnet/architecture/microservices/net-core-net-framework-containers/net-framework-container-scenarios)
