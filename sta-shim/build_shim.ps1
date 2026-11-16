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
    & cl.exe /nologo /O2 /MT /EHsc /std:c++17 /guard:cf /LD nanoserver_sta_shim.cpp /link /guard:cf /NODEFAULTLIB:ole32.lib /DEF:nanoserver_sta_shim.def /OUT:nanoserver_sta_shim_x86.dll mincore.lib kernel32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    # Export-only DLLs have no objects; their installed filename differs from
    # the architecture-suffixed payload. Preserve Windows' documented ordinals.
    foreach ($forwarder in @("olepro32", "comsvcs", "mtxdm")) {
        & link.exe /nologo /DLL /NOENTRY /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /IGNORE:4001,4070,4222 "/DEF:$forwarder.def" "/OUT:${forwarder}_x86.dll"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    & cl.exe /nologo /O2 /MT /EHsc /std:c++17 /guard:cf tests\native_regression.cpp /Fo:native_regression_x86.obj /link /guard:cf /OUT:NativeShimRegression_x86.exe kernel32.lib user32.lib gdi32.lib ole32.lib shlwapi.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cl.exe /nologo /O2 /MT /EHsc /std:c++17 /guard:cf tests\provider_lifetime_regression.cpp /Fo:provider_lifetime_x86.obj /link /guard:cf /OUT:ProviderLifetimeRegression_x86.exe kernel32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cl.exe /nologo /O2 /MT /EHsc /std:c++17 /guard:cf tests\mapped_installer_regression.cpp /Fo:mapped_installer_x86.obj /link /guard:cf /OUT:MappedInstallerRegression_x86.exe kernel32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cl.exe /nologo /O2 /MT /EHsc /std:c++17 /guard:cf tests\atom_regression.cpp /Fo:atom_regression_x86.obj /link /guard:cf /OUT:AtomRegression_x86.exe kernel32.lib user32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cl.exe /nologo /O2 /MT /guard:cf /LD tests\atom_fixture.cpp /Fo:atom_fixture_x86.obj /link /guard:cf /OUT:atom_fixture_x86.dll kernel32.lib user32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cl.exe /nologo /O2 /MT /EHsc /guard:cf /LD tests\teardown_fixture.cpp /Fo:teardown_fixture_x86.obj /link /guard:cf /DEF:tests\teardown_fixture.def /OUT:teardown_fixture_x86.dll kernel32.lib mtxdm.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cl.exe /nologo /O2 /MT /guard:cf tests\atom_static_regression.cpp /Fo:atom_static_x86.obj /link /guard:cf /OUT:AtomStaticRegression_x86.exe atom_fixture_x86.lib kernel32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cl.exe /nologo /O2 /MT /EHsc /std:c++17 /guard:cf tests\api_contract_regression.cpp /Fo:api_contract_x86.obj /link /guard:cf /OUT:ApiContractRegression_x86.exe kernel32.lib user32.lib gdi32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cl.exe /nologo /O2 /MT /EHsc /std:c++17 /guard:cf tests\crt_regression.cpp /Fo:crt_regression_x86.obj /link /guard:cf /OUT:CrtRegression_x86.exe kernel32.lib user32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "[2/3] Building nanoserver_sta_shim_x64.dll (MSVC $msvcVer, SDK $kitVer)..."
    $env:PATH    = "$msvcRoot\bin\Hostx64\x64;$kitRoot\bin\$kitVer\x64;$env:SystemRoot\System32"
    $env:INCLUDE = $incCommon
    $env:LIB     = "$msvcRoot\lib\x64;$kitRoot\Lib\$kitVer\ucrt\x64;$kitRoot\Lib\$kitVer\um\x64"
    & cl.exe /nologo /O2 /MT /EHsc /std:c++17 /guard:cf /LD nanoserver_sta_shim.cpp /link /guard:cf /NODEFAULTLIB:ole32.lib /DEF:nanoserver_sta_shim.def /OUT:nanoserver_sta_shim_x64.dll mincore.lib kernel32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & cl.exe /nologo /O2 /MT /EHsc /std:c++17 /guard:cf tests\native_regression.cpp /Fo:native_regression_x64.obj /link /guard:cf /OUT:NativeShimRegression_x64.exe kernel32.lib user32.lib gdi32.lib ole32.lib shlwapi.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cl.exe /nologo /O2 /MT /EHsc /std:c++17 /guard:cf tests\provider_lifetime_regression.cpp /Fo:provider_lifetime_x64.obj /link /guard:cf /OUT:ProviderLifetimeRegression_x64.exe kernel32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cl.exe /nologo /O2 /MT /EHsc /std:c++17 /guard:cf tests\mapped_installer_regression.cpp /Fo:mapped_installer_x64.obj /link /guard:cf /OUT:MappedInstallerRegression_x64.exe kernel32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cl.exe /nologo /O2 /MT /EHsc /std:c++17 /guard:cf tests\atom_regression.cpp /Fo:atom_regression_x64.obj /link /guard:cf /OUT:AtomRegression_x64.exe kernel32.lib user32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cl.exe /nologo /O2 /MT /guard:cf /LD tests\atom_fixture.cpp /Fo:atom_fixture_x64.obj /link /guard:cf /OUT:atom_fixture_x64.dll kernel32.lib user32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cl.exe /nologo /O2 /MT /EHsc /guard:cf /LD tests\teardown_fixture.cpp /Fo:teardown_fixture_x64.obj /link /guard:cf /DEF:tests\teardown_fixture.def /OUT:teardown_fixture_x64.dll kernel32.lib mtxdm.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cl.exe /nologo /O2 /MT /guard:cf tests\atom_static_regression.cpp /Fo:atom_static_x64.obj /link /guard:cf /OUT:AtomStaticRegression_x64.exe atom_fixture_x64.lib kernel32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cl.exe /nologo /O2 /MT /EHsc /std:c++17 /guard:cf tests\api_contract_regression.cpp /Fo:api_contract_x64.obj /link /guard:cf /OUT:ApiContractRegression_x64.exe kernel32.lib user32.lib gdi32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cl.exe /nologo /O2 /MT /EHsc /std:c++17 /guard:cf tests\crt_regression.cpp /Fo:crt_regression_x64.obj /link /guard:cf /OUT:CrtRegression_x64.exe kernel32.lib user32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "[3/3] Building install_sta_shim.exe (x64)..."
    foreach ($forwarder in @("olepro32", "comsvcs", "mtxdm")) {
        & link.exe /nologo /DLL /NOENTRY /MACHINE:X64 /DYNAMICBASE /NXCOMPAT /IGNORE:4001,4070,4222 "/DEF:$forwarder.def" "/OUT:${forwarder}_x64.dll"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    & cl.exe /nologo /O2 /MT /EHsc /std:c++17 /guard:cf install_sta_shim.cpp /link /guard:cf /OUT:install_sta_shim.exe kernel32.lib advapi32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "Building iis_service_monitor.exe (x64)..."
    & cl.exe /nologo /O2 /MT /EHsc /std:c++17 /guard:cf iis_service_monitor.cpp /link /guard:cf /OUT:iis_service_monitor.exe kernel32.lib advapi32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Remove-Item -LiteralPath "nanoserver_sta_shim.obj", "nanoserver_sta_shim.exp", "nanoserver_sta_shim.lib", "install_sta_shim.obj", "iis_service_monitor.obj", "native_regression_x86.obj", "native_regression_x64.obj" -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath "provider_lifetime_x86.obj", "provider_lifetime_x64.obj", "mapped_installer_x86.obj", "mapped_installer_x64.obj" -Force -ErrorAction SilentlyContinue
    foreach ($prefix in @("olepro32", "comsvcs", "mtxdm", "atom_fixture", "teardown_fixture")) {
        foreach ($architecture in @("x86", "x64")) {
            Remove-Item -LiteralPath "${prefix}_$architecture.lib", "${prefix}_$architecture.exp" -Force -ErrorAction SilentlyContinue
        }
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
        $targetDir = Join-Path (Split-Path $PSScriptRoot -Parent) "$flavour\sta-shim"
        if (-not (Test-Path $targetDir)) {
            New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
        }
        Copy-Item -Path "nanoserver_sta_shim_x86.dll", "nanoserver_sta_shim_x64.dll", "olepro32_x86.dll", "olepro32_x64.dll", "comsvcs_x86.dll", "comsvcs_x64.dll", "mtxdm_x86.dll", "mtxdm_x64.dll", "install_sta_shim.exe", "iis_service_monitor.exe" -Destination $targetDir -Force
        $architecture = if ($flavour.EndsWith("-x86")) { "x86" } else { "x64" }
        Copy-Item -LiteralPath "NativeShimRegression_$architecture.exe" -Destination (Join-Path (Split-Path $PSScriptRoot -Parent) "$flavour\Tests\native\NativeShimRegression.exe") -Force
        $nativeTests = Join-Path (Split-Path $PSScriptRoot -Parent) "$flavour\Tests\native"
        Copy-Item -LiteralPath "ProviderLifetimeRegression_$architecture.exe" -Destination (Join-Path $nativeTests "ProviderLifetimeRegression.exe") -Force
        Copy-Item -LiteralPath "AtomRegression_$architecture.exe" -Destination (Join-Path $nativeTests "AtomRegression.exe") -Force
        Copy-Item -LiteralPath "AtomStaticRegression_$architecture.exe" -Destination (Join-Path $nativeTests "AtomStaticRegression.exe") -Force
        Copy-Item -LiteralPath "atom_fixture_$architecture.dll" -Destination $nativeTests -Force
        Copy-Item -LiteralPath "teardown_fixture_$architecture.dll" -Destination $nativeTests -Force
    }

    $manifest = foreach ($payload in @("nanoserver_sta_shim_x86.dll", "nanoserver_sta_shim_x64.dll",
            "olepro32_x86.dll", "olepro32_x64.dll", "comsvcs_x86.dll", "comsvcs_x64.dll",
            "mtxdm_x86.dll", "mtxdm_x64.dll", "install_sta_shim.exe", "iis_service_monitor.exe")) {
        $expectedHash = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $payload) -Algorithm SHA256).Hash
        foreach ($flavour in $flavours) {
            $copy = Join-Path (Split-Path $PSScriptRoot -Parent) "$flavour\sta-shim\$payload"
            if ((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ne $expectedHash) {
                throw "Payload hash mismatch: $copy"
            }
        }
        [pscustomobject]@{ Payload = $payload; SHA256 = $expectedHash }
    }
    $artifactDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) ".artifacts"
    New-Item -ItemType Directory -Path $artifactDirectory -Force | Out-Null
    $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifactDirectory "sta-shim-binary-manifest.json") -Encoding utf8
    Write-Host "Done building and syncing STA shim binaries (60 SHA-256 matches)."
} finally {
    $env:PATH    = $origPath
    $env:INCLUDE = $origInclude
    $env:LIB     = $origLib
    Pop-Location
}
