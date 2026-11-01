Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$rootShimDir = Join-Path (Split-Path $PSScriptRoot -Parent) "sta-shim"
$localShimDir = Join-Path $PSScriptRoot "sta-shim"
if (-not (Test-Path -LiteralPath (Join-Path $rootShimDir "install_sta_shim.exe"))) {
    & (Join-Path $rootShimDir "build_shim.ps1")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
if (-not (Test-Path -LiteralPath $localShimDir)) {
    New-Item -ItemType Directory -Path $localShimDir -Force | Out-Null
}
Copy-Item -Path (Join-Path $rootShimDir "nanoserver_sta_shim_x86.dll"), (Join-Path $rootShimDir "nanoserver_sta_shim_x64.dll"), (Join-Path $rootShimDir "install_sta_shim.exe"), (Join-Path $rootShimDir "iis_service_monitor.exe") -Destination $localShimDir -Force

Push-Location $PSScriptRoot
try {
    docker build -t adaskothebeast/nanoserver:aspnet-x86 .
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}
