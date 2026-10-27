Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

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
