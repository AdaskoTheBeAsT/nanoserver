Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

docker run --rm adaskothebeast/nanoserver:iis-aspnet-x86-tests
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
