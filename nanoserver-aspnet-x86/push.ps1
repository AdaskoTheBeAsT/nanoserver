Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

docker image tag adaskothebeast/nanoserver:aspnet-x86 adaskothebeast/nanoserver:aspnet-x86-10.0.14-ltsc2025-v1.2.0
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image tag adaskothebeast/nanoserver:aspnet-x86 adaskothebeast/nanoserver:aspnet-x86-10.0-ltsc2025-v1.2
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image tag adaskothebeast/nanoserver:aspnet-x86 adaskothebeast/nanoserver:aspnet-x86-10-ltsc2025-v1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image push adaskothebeast/nanoserver:aspnet-x86
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image push adaskothebeast/nanoserver:aspnet-x86-10.0.14-ltsc2025-v1.2.0
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image push adaskothebeast/nanoserver:aspnet-x86-10.0-ltsc2025-v1.2
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image push adaskothebeast/nanoserver:aspnet-x86-10-ltsc2025-v1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
