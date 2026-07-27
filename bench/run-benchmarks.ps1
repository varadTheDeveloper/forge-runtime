<#
Phase 0 benchmark runner — Forge vs Bun vs Node.

Note: this script was written and reviewed carefully, but could not be
executed in the sandbox this was authored in (no PowerShell available
there) — it has not been run end-to-end by the author. Please report back
if anything in it errors out; it's a first pass, not a guaranteed-working
tool the way the C++ fixes earlier were.

Usage:
    .\run-benchmarks.ps1
    .\run-benchmarks.ps1 -ForgePath "C:\Forge\bin\forge.exe" -Iterations 7

Runs every .js benchmark script in this folder against whichever of
forge.exe / bun.exe / node.exe it can find, times each with
Measure-Command (wall-clock around the whole process, matching how the
architecture roadmap wants cold-start/throughput compared), and prints +
logs a results table so progress is a number you can watch move, not a
feeling.
#>

param(
    [string]$ForgePath = "C:\Forge\bin\forge.exe",
    [int]$Iterations = 5
)

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Benchmarks = @("startup.js", "json-bench.js", "loop-bench.js")

function Resolve-Runtime {
    param([string]$CommandName, [string]$ExplicitPath)

    if ($ExplicitPath -and (Test-Path $ExplicitPath)) {
        return $ExplicitPath
    }

    $cmd = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    return $null
}

$Runtimes = [ordered]@{
    "forge" = Resolve-Runtime "forge.exe" $ForgePath
    "bun"   = Resolve-Runtime "bun.exe" $null
    "node"  = Resolve-Runtime "node.exe" $null
}

Write-Host "Detected runtimes:"
foreach ($key in $Runtimes.Keys) {
    $path = $Runtimes[$key]
    if ($path) {
        Write-Host ("  {0,-6} -> {1}" -f $key, $path)
    } else {
        Write-Host ("  {0,-6} -> NOT FOUND (skipping)" -f $key)
    }
}
Write-Host ""

$Results = New-Object System.Collections.Generic.List[object]

foreach ($bench in $Benchmarks) {
    $scriptPath = Join-Path $ScriptDir $bench
    if (-not (Test-Path $scriptPath)) {
        Write-Warning "Missing benchmark script: $scriptPath"
        continue
    }

    foreach ($runtimeName in $Runtimes.Keys) {
        $exe = $Runtimes[$runtimeName]
        if (-not $exe) {
            continue
        }

        # One untimed run first: confirms the script actually runs and
        # prints the expected marker, and warms the OS file cache so run
        # #1 of the timed loop below isn't unfairly penalized.
        $output = & $exe $scriptPath 2>&1
        $exitCode = $LASTEXITCODE

        if ($exitCode -ne 0) {
            Write-Host ("{0,-16} {1,-6} FAILED (exit {2}): {3}" -f $bench, $runtimeName, $exitCode, ($output -join ' '))
            continue
        }

        $times = New-Object System.Collections.Generic.List[double]
        for ($i = 0; $i -lt $Iterations; $i++) {
            $elapsed = Measure-Command { & $exe $scriptPath *> $null }
            $times.Add($elapsed.TotalMilliseconds)
        }

        $avg = ($times | Measure-Object -Average).Average
        $min = ($times | Measure-Object -Minimum).Minimum

        $Results.Add([PSCustomObject]@{
            Benchmark = $bench
            Runtime   = $runtimeName
            AvgMs     = [math]::Round($avg, 2)
            MinMs     = [math]::Round($min, 2)
        })

        Write-Host ("{0,-16} {1,-6} avg={2,10:N2} ms   min={3,10:N2} ms" -f $bench, $runtimeName, $avg, $min)
    }
    Write-Host ""
}

Write-Host "=== Forge vs Bun (ratio < 1.0 means Forge is faster) ==="
foreach ($bench in $Benchmarks) {
    $forgeRow = $Results | Where-Object { $_.Benchmark -eq $bench -and $_.Runtime -eq "forge" }
    $bunRow   = $Results | Where-Object { $_.Benchmark -eq $bench -and $_.Runtime -eq "bun" }
    if ($forgeRow -and $bunRow) {
        $ratio = $forgeRow.AvgMs / $bunRow.AvgMs
        Write-Host ("{0,-16} forge/bun ratio = {1:N2}x" -f $bench, $ratio)
    }
}
Write-Host ""

# Save a timestamped log so results are comparable across sessions as
# each roadmap phase lands, not just visible in this one terminal.
$LogDir = Join-Path $ScriptDir "results"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$Timestamp = Get-Date -Format "yyyy-MM-dd_HHmmss"
$LogPath = Join-Path $LogDir "$Timestamp.csv"
$Results | Export-Csv -Path $LogPath -NoTypeInformation

Write-Host "Results saved to $LogPath"
