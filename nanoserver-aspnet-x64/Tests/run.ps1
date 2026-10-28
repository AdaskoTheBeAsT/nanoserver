Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

docker run --rm adaskothebeast/nanoserver:aspnet-x64-tests
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
