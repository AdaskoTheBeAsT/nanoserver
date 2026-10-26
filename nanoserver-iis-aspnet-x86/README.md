# Nano Server 2025 — IIS + x86 ASP.NET Core 10 Host & Sample API

## 1. Build, run, and push the reusable Host image

From this directory:

- Build: `go.bat` (`docker build -t adaskothebeast/nanoserver:iis-aspnet-x86 .`)
- Run: `run.bat` (`docker run --rm -it -p 8080:80 adaskothebeast/nanoserver:iis-aspnet-x86`)
- Tag & push: `push.bat`

Override the runtime version with `--build-arg DOTNET_VERSION=<version>`.

## 2. Build and run the Sample App image (`HealthApi`)

From `HealthApi\`:

- Build: `go.bat` (`docker build -t adaskothebeast/nanoserver:iis-aspnet-x86-sample .`)
- Run: `run.bat` (`docker run --rm -it -p 8080:80 adaskothebeast/nanoserver:iis-aspnet-x86-sample`)
- Verify: `curl.exe --fail http://localhost:8080/health` (returns HTTP 200 with `{"status":"Healthy"}`)
