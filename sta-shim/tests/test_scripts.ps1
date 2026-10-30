[CmdletBinding()]
param(
    [string]$ArtifactDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    $ArtifactDirectory = Join-Path $repo ".artifacts\roundtable-script-tests\$([guid]::NewGuid().ToString('N'))"
}
New-Item -ItemType Directory -Path $ArtifactDirectory -Force | Out-Null
$script:checks = 0
$script:failures = 0

function Assert-Check([string]$Name, [bool]$Condition) {
    $script:checks++
    if ($Condition) { Write-Host "CHECK $Name PASS" }
    else { Write-Host "CHECK $Name FAIL"; $script:failures++ }
}

function Invoke-Batch([string]$Path) {
    $output = & $env:ComSpec /d /c $Path 2>&1 | Out-String
    return @{ Code = $LASTEXITCODE; Output = $output }
}

$flavours = @(
    "nanoserver-aspnet-x64", "nanoserver-aspnet-x86",
    "nanoserver-iis-aspnet-x64", "nanoserver-iis-aspnet-x86",
    "nanoserver-iis-netfx-x64", "nanoserver-iis-netfx-x86"
)
$savedDism = $env:FAKE_DISM_CODE
$savedNative = $env:FAKE_NATIVE_CODE
$savedManaged = $env:FAKE_TEST_CODE
$savedFailAt = $env:FAKE_TEST_FAIL_AT

try {
    foreach ($flavour in $flavours) {
        $testDockerfile = Get-Content -LiteralPath (Join-Path $repo "$flavour\Tests\Dockerfile") -Raw
        $base = $flavour.Substring("nanoserver-".Length)
        Assert-Check "R16_${flavour}_candidate_base_override" (
            $testDockerfile.Contains("ARG BASE_IMAGE=adaskothebeast/nanoserver:$base") -and
            $testDockerfile.Contains('FROM ${BASE_IMAGE}') -and
            $testDockerfile.IndexOf("ARG BASE_IMAGE=") -lt $testDockerfile.IndexOf("FROM "))
        $source = Join-Path $repo "$flavour\install_features.cmd"
        if (-not (Test-Path -LiteralPath $source)) {
            $source = Join-Path $repo "$flavour\install_capabilities.cmd"
        }
        if (Test-Path -LiteralPath $source) {
            $body = Get-Content -LiteralPath $source -Raw
            $capabilities = [regex]::Matches($body, '(?m)^call :AddCap ').Count
            # Keep all batch control flow; replace only the external DISM call.
            $mocked = [regex]::Replace($body, '(?m)^DISM[^\r\n]*', 'cmd.exe /d /c exit %FAKE_DISM_CODE%')
            if ($mocked -eq $body) { throw "No DISM call replaced in $source" }
            $path = Join-Path $ArtifactDirectory "$flavour-capabilities.cmd"
            Set-Content -LiteralPath $path -Value $mocked -NoNewline -Encoding ascii
            foreach ($code in @(0, 3010, 3011, -2146498529, -1073741819)) {
                $env:FAKE_DISM_CODE = "$code"
                $result = Invoke-Batch $path
                $expected = if ($code -eq 0 -or $code -eq 3010) { 0 } else { $code }
                $expectedCalls = if ($expected -eq 0) { $capabilities } else { 1 }
                $calls = [regex]::Matches($result.Output, '(?m)^Installing capability:').Count
                Assert-Check "R11_${flavour}_exit_$code" ($result.Code -eq $expected -and $calls -eq $expectedCalls)
            }
        }

        $body = Get-Content -LiteralPath (Join-Path $repo "$flavour\Tests\run_all_modes.cmd") -Raw
        $mocked = [regex]::Replace($body, '(?m)^cd /d C:\\tests\s*$', 'cd /d "%~dp0"')
        $mocked = $mocked.Replace('C:\tests\NativeShimRegression.exe --require-shim',
            'cmd.exe /d /c exit %FAKE_NATIVE_CODE%')
        $pattern = '(?m)^(?:dotnet exec C:\\tests\\[^\r\n]*|C:\\tests\\AdaskoTheBeAsT\.Interop\.COM\.Test\.exe[^\r\n]*)'
        Assert-Check "R12_${flavour}_three_managed_commands" ([regex]::Matches($mocked, $pattern).Count -eq 3)
        $options = "AdaskoTheBeAsT.Interop.COM.Test.NanoServerFallbackTest.NanoServerOptionsShouldManageStaShimStateAndIdentifyShimmedApiSets"
        $callbacks = "AdaskoTheBeAsT.Interop.COM.Test.NanoServerWindowManagerTest.NanoServerStaShimCallbacksShouldHandleApiSetPresenceDelayLoadAndModuleHandles"
        Assert-Check "R16_${flavour}_stable_suite_excludes_only_mutators" (
            $body.Contains("--filter-not-method `"$options`" --filter-not-method `"$callbacks`""))
        Assert-Check "R16_${flavour}_mutators_are_not_omitted" (
            $body.Contains("--filter-method `"$options`"") -and
            $body.Contains("--filter-method `"$callbacks`""))
        $mocked = [regex]::Replace($mocked, $pattern, "call :MockManaged")
        $mocked += @'

:MockManaged
set /a MANAGED_CALL_COUNT+=1 >nul
echo __MANAGED__
if "%MANAGED_CALL_COUNT%"=="%FAKE_TEST_FAIL_AT%" exit /b %FAKE_TEST_CODE%
exit /b 0

'@
        $path = Join-Path $ArtifactDirectory "$flavour-modes.cmd"
        Set-Content -LiteralPath $path -Value $mocked -NoNewline -Encoding ascii
        $cases = @(
            @{ Native = 0; Managed = 0; Expected = 0; Calls = 11; FailAt = 0 },
            @{ Native = 3; Managed = 0; Expected = 3; Calls = 0; FailAt = 0 },
            @{ Native = -1073741819; Managed = 0; Expected = -1073741819; Calls = 0; FailAt = 0 }
        )
        foreach ($call in 1..11) {
            $cases += @{ Native = 0; Managed = 1; Expected = 1; Calls = $call; FailAt = $call }
        }
        $cases += @{ Native = 0; Managed = -1073741819; Expected = -1073741819; Calls = 11; FailAt = 11 }
        foreach ($case in $cases) {
            $env:FAKE_NATIVE_CODE = "$($case.Native)"
            $env:FAKE_TEST_CODE = "$($case.Managed)"
            $env:FAKE_TEST_FAIL_AT = "$($case.FailAt)"
            $result = Invoke-Batch $path
            $calls = [regex]::Matches($result.Output, '(?m)^__MANAGED__\s*$').Count
            $reportedSuccess = $result.Output.Contains("ALL 4 MODES PASSED SUCCESSFULLY!")
            Assert-Check "R12_${flavour}_native_$($case.Native)_managed_$($case.Managed)_at_$($case.FailAt)" (
                $result.Code -eq $case.Expected -and $calls -eq $case.Calls -and
                $reportedSuccess -eq ($case.Expected -eq 0))
        }

        $dockerfile = Get-Content -LiteralPath (Join-Path $repo "$flavour\Dockerfile") -Raw
        if ($flavour -ne "nanoserver-aspnet-x64") {
            Assert-Check "R11_${flavour}_wow64_marker" ($dockerfile.Contains("SysWOW64\ntdll.dll"))
            Assert-Check "R02_${flavour}_requires_wow64" ($dockerfile.Contains("--require-wow64"))
        }
        if ($flavour.Contains("-netfx-")) {
            $copy = $dockerfile.IndexOf("COPY --from=installer")
            $wow = $dockerfile.IndexOf("SysWOW64\ntdll.dll")
            $iis = $dockerfile.IndexOf("appcmd.exe")
            Assert-Check "R11_${flavour}_markers_before_servercore_copy" (
                $copy -gt 0 -and $wow -gt 0 -and $iis -gt 0 -and $wow -lt $copy -and $iis -lt $copy)
        }
        if ($flavour.Contains("-iis-")) {
            Assert-Check "R14_${flavour}_foreground_monitor" (
                $dockerfile.Contains('CMD ["C:\\iis_service_monitor.exe"]'))
        }
    }

    # Run the actual version updater and publish scripts in a fresh session.
    # Docker is a PowerShell function in this sandbox: no images are tagged/pushed.
    $publish = Join-Path $ArtifactDirectory "publish"
    New-Item -ItemType Directory -Path $publish | Out-Null
    Copy-Item -LiteralPath (Join-Path $repo "push.ps1"), (Join-Path $repo "set-version.ps1") -Destination $publish
    foreach ($flavour in $flavours) {
        $folder = Join-Path $publish $flavour
        New-Item -ItemType Directory -Path $folder | Out-Null
        Set-Content -LiteralPath (Join-Path $folder "Dockerfile") -Value "ARG DOTNET_VERSION=10.0.14"
        Set-Content -LiteralPath (Join-Path $folder "push.ps1") -Value "# Replaced by the real version updater"
    }
    $mock = @'
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
function global:docker {
    param([Parameter(ValueFromRemainingArguments = $true)][object[]]$DockerArgs)
    if ($DockerArgs.Count -lt 2 -or $DockerArgs[0] -ne "image" -or $DockerArgs[1] -notin @("tag", "push")) {
        throw "Unexpected mocked Docker command"
    }
    Add-Content -LiteralPath (Join-Path $PSScriptRoot "calls.log") -Value ($DockerArgs -join " ")
    $global:LASTEXITCODE = 0
}
Remove-Variable -Name LASTEXITCODE -Scope Global -ErrorAction SilentlyContinue
& (Join-Path $PSScriptRoot "push.ps1") -Version 1.3.0
'@
    $mockPath = Join-Path $publish "mock_publish.ps1"
    Set-Content -LiteralPath $mockPath -Value $mock
    $output = & (Join-Path $PSHOME "pwsh.exe") -NoProfile -File $mockPath 2>&1 | Out-String
    $publishExit = $LASTEXITCODE
    Set-Content -LiteralPath (Join-Path $publish "output.log") -Value $output
    Assert-Check "R13_fresh_session_mocked_publish" ($publishExit -eq 0)
    $callsPath = Join-Path $publish "calls.log"
    $calls = if (Test-Path -LiteralPath $callsPath) { @(Get-Content -LiteralPath $callsPath) } else { @() }
    Assert-Check "R13_all_six_flavours_reached" ($calls.Count -eq 42)
    foreach ($flavour in $flavours) {
        $content = Get-Content -LiteralPath (Join-Path $publish "$flavour\push.ps1") -Raw
        Assert-Check "version_${flavour}_1_3_0" ($content.Contains("-ltsc2025-v1.3.0") -and
            $content.Contains("-ltsc2025-v1.3") -and $content.Contains("-ltsc2025-v1"))
    }
} finally {
    $env:FAKE_DISM_CODE = $savedDism
    $env:FAKE_NATIVE_CODE = $savedNative
    $env:FAKE_TEST_CODE = $savedManaged
    $env:FAKE_TEST_FAIL_AT = $savedFailAt
}

Write-Host "SUMMARY scripts checks=$script:checks failures=$script:failures artifacts=$ArtifactDirectory"
if ($script:failures) { exit 1 }
