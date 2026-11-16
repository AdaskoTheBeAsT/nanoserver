[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string[]]$Images,
    [Parameter(Mandatory)]
    [string]$ArtifactDirectory,
    [string]$DockerHost = "npipe:////./pipe/docker_engine_windows"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$probeBinary = Join-Path (Split-Path $PSScriptRoot -Parent) "ApiContractRegression_x64.exe"
if (!(Test-Path -LiteralPath $probeBinary -PathType Leaf)) { throw "Build the native test probes first." }
New-Item -ItemType Directory -Path $ArtifactDirectory -Force | Out-Null
$ArtifactDirectory = (Resolve-Path -LiteralPath $ArtifactDirectory).Path
$probeFile = Join-Path $ArtifactDirectory "native-iis-probe.htm"
Set-Content -LiteralPath $probeFile -Value "native-iis-probe" -Encoding ascii

function Invoke-Docker {
    param([string[]]$Arguments, [int]$TimeoutSeconds = 60, [switch]$IncludeStandardError)
    $start = [System.Diagnostics.ProcessStartInfo]::new("docker.exe")
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in @("--host", $DockerHost) + $Arguments) {
        $start.ArgumentList.Add($argument)
    }
    $process = [System.Diagnostics.Process]::Start($start)
    try {
        $output = $process.StandardOutput.ReadToEndAsync()
        $errors = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill($true)
            $process.WaitForExit()
            throw "Docker command timed out after ${TimeoutSeconds}s: $($Arguments -join ' ')"
        }
        $text = $output.GetAwaiter().GetResult()
        $errorText = $errors.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) {
            throw "Docker exited $($process.ExitCode): $errorText$text"
        }
        if ($IncludeStandardError) { return ($text + $errorText).Trim() }
        return $text.Trim()
    } finally {
        $process.Dispose()
    }
}

$results = [System.Collections.Generic.List[object]]::new()
foreach ($image in $Images) {
    $imageId = Invoke-Docker -Arguments @("image", "inspect", "--format", "{{.Id}}", $image)
    $label = $image -replace '[^A-Za-z0-9_.-]', '-'
    foreach ($scenario in @("w3svc-stop", "was-host-loss")) {
        $name = "nano-iis-qualification-$([guid]::NewGuid().ToString('N'))"
        $id = $null
        $scenarioFailure = $null
        try {
            # No entrypoint override and no published host port.
            $id = Invoke-Docker -Arguments @("create", "--name", $name, "--isolation=process", $image)
            Invoke-Docker -Arguments @("start", $id) | Out-Null
            $remoteFile = "C:\inetpub\wwwroot\native-iis-probe.htm"
            Invoke-Docker -Arguments @("cp", $probeFile, "${id}:$remoteFile") | Out-Null
            Invoke-Docker -Arguments @("exec", $id, "icacls.exe", $remoteFile, "/grant",
                "*S-1-5-32-568:R", "*S-1-5-32-545:R", "/Q") | Out-Null
            $healthy = $false
            $lastProbe = ""
            $deadline = [DateTime]::UtcNow.AddSeconds(90)
            do {
                try {
                    # Host-to-container NAT routing is not available on every
                    # Windows host. Exercise real HTTP without publishing ports.
                    $lastProbe = Invoke-Docker -Arguments @("exec", $id, "curl.exe", "--fail",
                        "--silent", "--show-error", "--noproxy", "*", "--max-time", "3",
                        "--write-out", '\n%{http_code}', "http://localhost/native-iis-probe.htm")
                    $healthy = $lastProbe -match '(?s)^native-iis-probe\s+200$'
                } catch {
                    $lastProbe = $_.Exception.Message
                    $healthy = $false
                }
                if (!$healthy) { Start-Sleep -Milliseconds 500 }
            } until ($healthy -or [DateTime]::UtcNow -ge $deadline)
            if (!$healthy) { throw "Default startup did not serve the IIS HTTP probe. Last result: $lastProbe" }
            Write-Host "CHECK $image $scenario default_startup_http_200 PASS"
            if ($scenario -eq "w3svc-stop") {
                Invoke-Docker -Arguments @("exec", $id, "sc.exe", "stop", "W3SVC") | Out-Null
            } else {
                Invoke-Docker -Arguments @("cp", $probeBinary, "${id}:C:\native-iis-probe.exe") | Out-Null
                # Detached exec avoids a race with the monitor ending the
                # container before Docker collects the injector's exit status.
                # Failure to kill WAS still fails the wait/log checks below.
                Invoke-Docker -Arguments @("exec", "--detach", $id,
                    "C:\native-iis-probe.exe", "--terminate-was-host") | Out-Null
            }
            # Windows Server 2025 endpoint rundown can take 120 seconds.
            $exitCode = Invoke-Docker -Arguments @("wait", $id) -TimeoutSeconds 150
            if ($exitCode -ne "1") { throw "Expected monitor exit 1; received '$exitCode'." }
            $log = Invoke-Docker -Arguments @("logs", $id) -IncludeStandardError
            Set-Content -LiteralPath (Join-Path $ArtifactDirectory "$label-$scenario.log") -Value $log
            if ($log -notmatch '(WAS|W3SVC) stopped') { throw "Missing service-loss discriminator in monitor log." }
            Write-Host "CHECK $image $scenario monitor_exit_1 PASS"
            $results.Add([pscustomobject]@{ Image = $image; ImageId = $imageId; Scenario = $scenario; Http = 200; ExitCode = 1 })
        } catch {
            $scenarioFailure = $_
            throw
        } finally {
            if ($id) {
                $cleanupFailure = $null
                if ($scenario -eq "was-host-loss") {
                    try {
                        Invoke-Docker -Arguments @("cp", "${id}:C:\Temp\native-was-loss.log",
                            (Join-Path $ArtifactDirectory "$label-$scenario-injector.log")) | Out-Null
                    } catch {
                        # Optional diagnostic; wait and monitor logs are the gate.
                    }
                }
                try {
                    $log = Invoke-Docker -Arguments @("logs", $id) -IncludeStandardError
                    Set-Content -LiteralPath (Join-Path $ArtifactDirectory "$label-$scenario.log") -Value $log
                } catch {
                    $cleanupFailure = $_
                }
                try {
                    Invoke-Docker -Arguments @("rm", "--force", $id) -TimeoutSeconds 180 | Out-Null
                } catch {
                    if (!$cleanupFailure) { $cleanupFailure = $_ }
                }
                if ($cleanupFailure) {
                    if ($scenarioFailure) { Write-Warning $cleanupFailure.Exception.Message }
                    else { throw $cleanupFailure }
                }
            }
        }
    }
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ArtifactDirectory "results.json")
Write-Host "SUMMARY IIS runtime cases=$($results.Count) failures=0"
