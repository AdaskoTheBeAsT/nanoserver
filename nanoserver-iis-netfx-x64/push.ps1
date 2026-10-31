Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

docker image tag adaskothebeast/nanoserver:iis-netfx-x64 adaskothebeast/nanoserver:iis-netfx-x64-4.8.1-ltsc2025-v1.4.0
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image tag adaskothebeast/nanoserver:iis-netfx-x64 adaskothebeast/nanoserver:iis-netfx-x64-4.8-ltsc2025-v1.4
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image tag adaskothebeast/nanoserver:iis-netfx-x64 adaskothebeast/nanoserver:iis-netfx-x64-4-ltsc2025-v1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image push adaskothebeast/nanoserver:iis-netfx-x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image push adaskothebeast/nanoserver:iis-netfx-x64-4.8.1-ltsc2025-v1.4.0
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image push adaskothebeast/nanoserver:iis-netfx-x64-4.8-ltsc2025-v1.4
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker image push adaskothebeast/nanoserver:iis-netfx-x64-4-ltsc2025-v1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
