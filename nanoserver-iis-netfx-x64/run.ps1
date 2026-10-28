Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

docker run --rm -it -p 8080:80 adaskothebeast/nanoserver:iis-netfx-x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
