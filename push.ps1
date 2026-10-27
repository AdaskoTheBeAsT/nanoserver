[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [ValidatePattern('^v?\d+\.\d+\.\d+$')]
    [string]$Version,

    [Parameter(Mandatory = $false)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$DotNetVersion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not [string]::IsNullOrWhiteSpace($Version)) {
    $setVersionArgs = @{ Version = $Version }
    if (-not [string]::IsNullOrWhiteSpace($DotNetVersion)) {
        $setVersionArgs.DotNetVersion = $DotNetVersion
    }
    & (Join-Path $PSScriptRoot "set-version.ps1") @setVersionArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$flavours = @(
    "nanoserver-aspnet-x86",
    "nanoserver-aspnet-x64",
    "nanoserver-iis-aspnet-x64",
    "nanoserver-iis-aspnet-x86",
    "nanoserver-iis-netfx-x64",
    "nanoserver-iis-netfx-x86"
)

foreach ($flavour in $flavours) {
    & (Join-Path $PSScriptRoot "$flavour\push.ps1")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
