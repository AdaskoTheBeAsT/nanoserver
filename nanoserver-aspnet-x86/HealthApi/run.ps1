Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

docker run --rm -it -p 8080:8080 adaskothebeast/nanoserver:aspnet-x86-sample
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
