Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

docker run --rm -it -p 8080:80 adaskothebeast/nanoserver:iis-netfx-x86-sample
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
