Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found at $vswhere"
}

$vsDir = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1).Trim()
if ([string]::IsNullOrWhiteSpace($vsDir)) {
    throw "Visual Studio with C++ x86/x64 tools not found."
}

$msvcVer = (Get-ChildItem (Join-Path $vsDir "VC\Tools\MSVC") -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$kitRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10"
$kitVer  = (Get-ChildItem (Join-Path $kitRoot "Include") -Directory | Where-Object { Test-Path (Join-Path $_.FullName "um\windows.h") } | Sort-Object Name -Descending | Select-Object -First 1).Name

$msvcRoot  = Join-Path $vsDir "VC\Tools\MSVC\$msvcVer"
$incCommon = "$msvcRoot\include;$kitRoot\Include\$kitVer\ucrt;$kitRoot\Include\$kitVer\shared;$kitRoot\Include\$kitVer\um"

$origPath    = $env:PATH
$origInclude = $env:INCLUDE
$origLib     = $env:LIB

Push-Location $PSScriptRoot
try {
    Write-Host "[1/3] Building nanoserver_sta_shim_x86.dll (MSVC $msvcVer, SDK $kitVer)..."
    $env:PATH    = "$msvcRoot\bin\Hostx64\x86;$kitRoot\bin\$kitVer\x64;$env:SystemRoot\System32"
    $env:INCLUDE = $incCommon
    $env:LIB     = "$msvcRoot\lib\x86;$kitRoot\Lib\$kitVer\ucrt\x86;$kitRoot\Lib\$kitVer\um\x86"
    & cl.exe /nologo /O2 /MT /EHsc /LD nanoserver_sta_shim.cpp /link /NODEFAULTLIB:ole32.lib /DEF:nanoserver_sta_shim.def /OUT:nanoserver_sta_shim_x86.dll mincore.lib kernel32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "[2/3] Building nanoserver_sta_shim_x64.dll (MSVC $msvcVer, SDK $kitVer)..."
    $env:PATH    = "$msvcRoot\bin\Hostx64\x64;$kitRoot\bin\$kitVer\x64;$env:SystemRoot\System32"
    $env:INCLUDE = $incCommon
    $env:LIB     = "$msvcRoot\lib\x64;$kitRoot\Lib\$kitVer\ucrt\x64;$kitRoot\Lib\$kitVer\um\x64"
    & cl.exe /nologo /O2 /MT /EHsc /LD nanoserver_sta_shim.cpp /link /NODEFAULTLIB:ole32.lib /DEF:nanoserver_sta_shim.def /OUT:nanoserver_sta_shim_x64.dll mincore.lib kernel32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "[3/3] Building install_sta_shim.exe (x64)..."
    & cl.exe /nologo /O2 /MT /EHsc install_sta_shim.cpp /link /OUT:install_sta_shim.exe kernel32.lib advapi32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Remove-Item -Path *.obj, *.exp, *.lib -Force -ErrorAction SilentlyContinue

    $flavours = @(
        "nanoserver-aspnet-x86",
        "nanoserver-aspnet-x64",
        "nanoserver-iis-aspnet-x64",
        "nanoserver-iis-aspnet-x86",
        "nanoserver-iis-netfx-x64",
        "nanoserver-iis-netfx-x86"
    )

    foreach ($flavour in $flavours) {
        $targetDir = Join-Path (Split-Path $PSScriptRoot -Parent) "$flavour\sta-shim"
        if (-not (Test-Path $targetDir)) {
            New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
        }
        Copy-Item -Path "nanoserver_sta_shim_x86.dll", "nanoserver_sta_shim_x64.dll", "install_sta_shim.exe" -Destination $targetDir -Force
    }

    Write-Host "Done building and syncing STA shim binaries."
} finally {
    $env:PATH    = $origPath
    $env:INCLUDE = $origInclude
    $env:LIB     = $origLib
    Pop-Location
}
