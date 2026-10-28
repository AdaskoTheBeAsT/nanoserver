<#
.SYNOPSIS
    Updates the 3-tier version tags (x.x.x, x.x, x) in all subfolder push.ps1 files.

.DESCRIPTION
    Takes a semantic version in x.x.x (or vx.x.x) format, splits it into:
      - Full  (x.x.x, e.g. v1.1.0)
      - Minor (x.x,   e.g. v1.1)
      - Major (x,     e.g. v1)
    and updates both the `docker image tag` and `docker image push` commands
    across every nanoserver-* subfolder's push.ps1 file.

    Optionally accepts -DotNetVersion (e.g. 10.0.14) and -NetFxVersion (e.g. 4.8.1)
    to update the runtime version segments at the same time.

.EXAMPLE
    .\set-version.ps1 -Version 1.1.0

.EXAMPLE
    .\set-version.ps1 1.2.3 -DotNetVersion 10.0.14
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0, HelpMessage = "Container version in x.x.x format (e.g. 1.1.0 or v1.1.0).")]
    [ValidatePattern('^v?\d+\.\d+\.\d+$')]
    [string]$Version,

    [Parameter(Mandatory = $false, HelpMessage = "Optional .NET runtime version in x.x.x format (default: read from Dockerfile or 10.0.14).")]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$DotNetVersion,

    [Parameter(Mandatory = $false, HelpMessage = "Optional .NET Framework runtime version in x.x.x format (default: 4.8.1).")]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$NetFxVersion = "4.8.1"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$cleanVersion = $Version.TrimStart('v', 'V')
$vParts = $cleanVersion.Split('.')
$vFull  = "$($vParts[0]).$($vParts[1]).$($vParts[2])"
$vMinor = "$($vParts[0]).$($vParts[1])"
$vMajor = "$($vParts[0])"

$repoRoot = $PSScriptRoot

$flavours = @(
    @{ Folder = "nanoserver-aspnet-x86";     Tag = "aspnet-x86";     IsNetFx = $false },
    @{ Folder = "nanoserver-aspnet-x64";     Tag = "aspnet-x64";     IsNetFx = $false },
    @{ Folder = "nanoserver-iis-aspnet-x64"; Tag = "iis-aspnet-x64"; IsNetFx = $false },
    @{ Folder = "nanoserver-iis-aspnet-x86"; Tag = "iis-aspnet-x86"; IsNetFx = $false },
    @{ Folder = "nanoserver-iis-netfx-x64";  Tag = "iis-netfx-x64";  IsNetFx = $true  },
    @{ Folder = "nanoserver-iis-netfx-x86";  Tag = "iis-netfx-x86";  IsNetFx = $true  }
)

foreach ($f in $flavours) {
    $folderPath = Join-Path $repoRoot $f.Folder
    if (-not (Test-Path $folderPath)) {
        continue
    }

    $pushPs1Path    = Join-Path $folderPath "push.ps1"
    $dockerfilePath = Join-Path $folderPath "Dockerfile"

    if ($f.IsNetFx) {
        $rtFull = $NetFxVersion
    } elseif (-not [string]::IsNullOrWhiteSpace($DotNetVersion)) {
        $rtFull = $DotNetVersion
        if (Test-Path $dockerfilePath) {
            $dfContent = Get-Content $dockerfilePath -Raw
            $dfUpdated = [regex]::Replace($dfContent, '(?m)^ARG\s+DOTNET_VERSION=\S+', "ARG DOTNET_VERSION=$DotNetVersion")
            if ($dfUpdated -ne $dfContent) {
                Set-Content -Path $dockerfilePath -Value $dfUpdated -NoNewline
                Write-Host "[OK] Updated DOTNET_VERSION=$DotNetVersion in $($f.Folder)\Dockerfile"
            }
        }
    } else {
        $rtFull = "10.0.14"
        if (Test-Path $dockerfilePath) {
            $match = [regex]::Match((Get-Content $dockerfilePath -Raw), '(?m)^ARG\s+DOTNET_VERSION=(\d+\.\d+\.\d+)')
            if ($match.Success) {
                $rtFull = $match.Groups[1].Value
            }
        }
    }

    $rtParts = $rtFull.Split('.')
    $rtMinor = "$($rtParts[0]).$($rtParts[1])"
    $rtMajor = "$($rtParts[0])"

    $baseImage = "adaskothebeast/nanoserver:$($f.Tag)"
    $tagFull   = "${baseImage}-${rtFull}-ltsc2025-v${vFull}"
    $tagMinor  = "${baseImage}-${rtMinor}-ltsc2025-v${vMinor}"
    $tagMajor  = "${baseImage}-${rtMajor}-ltsc2025-v${vMajor}"

    $lines = @(
        "Set-StrictMode -Version Latest",
        "`$ErrorActionPreference = `"Stop`"",
        "",
        "docker image tag $baseImage $tagFull",
        "if (`$LASTEXITCODE -ne 0) { exit `$LASTEXITCODE }",
        "docker image tag $baseImage $tagMinor",
        "if (`$LASTEXITCODE -ne 0) { exit `$LASTEXITCODE }",
        "docker image tag $baseImage $tagMajor",
        "if (`$LASTEXITCODE -ne 0) { exit `$LASTEXITCODE }",
        "docker image push $baseImage",
        "if (`$LASTEXITCODE -ne 0) { exit `$LASTEXITCODE }",
        "docker image push $tagFull",
        "if (`$LASTEXITCODE -ne 0) { exit `$LASTEXITCODE }",
        "docker image push $tagMinor",
        "if (`$LASTEXITCODE -ne 0) { exit `$LASTEXITCODE }",
        "docker image push $tagMajor",
        "if (`$LASTEXITCODE -ne 0) { exit `$LASTEXITCODE }"
    )

    $newContent = ($lines -join "`r`n") + "`r`n"
    Set-Content -Path $pushPs1Path -Value $newContent -NoNewline
    Write-Host "[OK] Updated $($f.Folder)\push.ps1 -> v$vFull | v$vMinor | v$vMajor (runtime: $rtFull | $rtMinor | $rtMajor)"
}
