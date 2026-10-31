Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

docker image tag adaskothebeast/nanoserver:iis-aspnet-x64 adaskothebeast/nanoserver:iis-aspnet-x64-10.0.14-ltsc2025-v1.4.0
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image tag adaskothebeast/nanoserver:iis-aspnet-x64 adaskothebeast/nanoserver:iis-aspnet-x64-10.0-ltsc2025-v1.4
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image tag adaskothebeast/nanoserver:iis-aspnet-x64 adaskothebeast/nanoserver:iis-aspnet-x64-10-ltsc2025-v1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image push adaskothebeast/nanoserver:iis-aspnet-x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image push adaskothebeast/nanoserver:iis-aspnet-x64-10.0.14-ltsc2025-v1.4.0
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image push adaskothebeast/nanoserver:iis-aspnet-x64-10.0-ltsc2025-v1.4
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image push adaskothebeast/nanoserver:iis-aspnet-x64-10-ltsc2025-v1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
