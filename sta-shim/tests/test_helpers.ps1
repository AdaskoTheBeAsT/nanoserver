[CmdletBinding()]
param(
    [string]$ArtifactDirectory,
    [switch]$SyntaxOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    $ArtifactDirectory = Join-Path $repo ".artifacts\roundtable-helper-tests\$([guid]::NewGuid().ToString('N'))"
}
New-Item -ItemType Directory -Path $ArtifactDirectory -Force | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vs = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1).Trim()
if ([string]::IsNullOrWhiteSpace($vs)) { throw "Visual Studio C++ tools not found." }
$msvc = (Get-ChildItem (Join-Path $vs "VC\Tools\MSVC") -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$kit = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10"
$sdk = (Get-ChildItem (Join-Path $kit "Include") -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName "um\windows.h") } |
    Sort-Object Name -Descending | Select-Object -First 1).Name
$saved = @{ Path = $env:PATH; Include = $env:INCLUDE; Lib = $env:LIB }
try {
    $env:INCLUDE = "$msvc\include;$kit\Include\$sdk\ucrt;$kit\Include\$sdk\shared;$kit\Include\$sdk\um"
    $architectures = if ($SyntaxOnly) { @("x86", "x64") } else { @("x64") }
    $names = if ($SyntaxOnly) { @("installer", "monitor", "hook", "native") } else { @("installer", "monitor", "hook") }
    foreach ($architecture in $architectures) {
        $env:PATH = "$msvc\bin\Hostx64\$architecture;$kit\bin\$sdk\x64;$env:SystemRoot\System32"
        $env:LIB = "$msvc\lib\$architecture;$kit\Lib\$sdk\ucrt\$architecture;$kit\Lib\$sdk\um\$architecture"
        foreach ($name in $names) {
            $source = Join-Path $PSScriptRoot "${name}_regression.cpp"
            if ($SyntaxOnly) {
                Write-Host "Syntax only: $name ($architecture)"
                & cl.exe /nologo /Zs /MT /EHsc /std:c++17 $source
                if ($LASTEXITCODE -ne 0) { throw "Checking $name syntax failed ($LASTEXITCODE)." }
                continue
            }
            $object = Join-Path $ArtifactDirectory "$name.obj"
            $exe = Join-Path $ArtifactDirectory "$name.exe"
            & cl.exe /nologo /O2 /MT /EHsc /std:c++17 /guard:cf $source "/Fo:$object" /link /guard:cf "/OUT:$exe" mincore.lib kernel32.lib advapi32.lib
            if ($LASTEXITCODE -ne 0) { throw "Compiling $name regression failed ($LASTEXITCODE)." }
            $output = & $exe 2>&1 | Out-String
            $code = $LASTEXITCODE
            Set-Content -LiteralPath (Join-Path $ArtifactDirectory "$name.log") -Value $output
            Write-Host $output
            if ($code -ne 0) { throw "$name regression failed ($code). See $ArtifactDirectory." }
        }
    }
} finally {
    $env:PATH = $saved.Path
    $env:INCLUDE = $saved.Include
    $env:LIB = $saved.Lib
}
if ($SyntaxOnly) { Write-Host "SUMMARY native helper syntax checks passed (x86/x64; no binaries generated)" }
else { Write-Host "SUMMARY native helpers passed; artifacts=$ArtifactDirectory" }
