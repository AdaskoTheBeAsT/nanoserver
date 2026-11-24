Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$dockerOs = docker info --format '{{.OSType}}'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($dockerOs -ne "windows") {
    throw "Nano Server images require a Windows Docker engine (current: $dockerOs). If Docker Desktop has a desktop-windows context, set `$env:DOCKER_CONTEXT = 'desktop-windows' in this PowerShell session and retry."
}

& (Join-Path $PSScriptRoot "sta-shim\build_shim.ps1")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$flavours = @(
    "nanoserver-aspnet-x86",
    "nanoserver-aspnet-x64",
    "nanoserver-iis-aspnet-x64",
    "nanoserver-iis-aspnet-x86",
    "nanoserver-iis-netfx-x64",
    "nanoserver-iis-netfx-x86"
)

foreach ($flavour in $flavours) {
    & (Join-Path $PSScriptRoot "$flavour\go.ps1")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
