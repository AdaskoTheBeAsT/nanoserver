Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

docker image tag adaskothebeast/nanoserver:aspnet-x86 adaskothebeast/nanoserver:aspnet-x86-11.0.0-ltsc2025-v2.1.0
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image tag adaskothebeast/nanoserver:aspnet-x86 adaskothebeast/nanoserver:aspnet-x86-11.0-ltsc2025-v2.1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image tag adaskothebeast/nanoserver:aspnet-x86 adaskothebeast/nanoserver:aspnet-x86-11-ltsc2025-v2
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image push adaskothebeast/nanoserver:aspnet-x86
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image push adaskothebeast/nanoserver:aspnet-x86-11.0.0-ltsc2025-v2.1.0
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image push adaskothebeast/nanoserver:aspnet-x86-11.0-ltsc2025-v2.1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image push adaskothebeast/nanoserver:aspnet-x86-11-ltsc2025-v2
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
