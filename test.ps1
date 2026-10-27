Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$flavours = @(
    "nanoserver-aspnet-x86",
    "nanoserver-aspnet-x64",
    "nanoserver-iis-aspnet-x64",
    "nanoserver-iis-aspnet-x86",
    "nanoserver-iis-netfx-x64",
    "nanoserver-iis-netfx-x86"
)

foreach ($flavour in $flavours) {
    & (Join-Path $PSScriptRoot "$flavour\Tests\go.ps1")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & (Join-Path $PSScriptRoot "$flavour\Tests\run.ps1")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
