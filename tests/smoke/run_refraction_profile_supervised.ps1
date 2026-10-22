#requires -Version 7.0
<#
Run this supervisor through Start-Process -WindowStyle Hidden so its lifetime is
independent of the originating interactive command. It supervises one complete
fixed campaign; it never resumes partial trials or selectively replays cells.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RepositoryRoot,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$FrozenArm,
    [string]$ExpectedPlan,
    [string]$PythonExecutable,
    [switch]$ProbeOnly,
    [ValidateRange(1,300)][int]$ProbeSeconds = 60
)

$ErrorActionPreference = 'Stop'

function Write-StatusJson([string]$Path, [object]$Value){
    $temporary = $Path + '.tmp'
    $json = ConvertTo-Json -InputObject $Value -Depth 30
    [IO.File]::WriteAllText($temporary, $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
    [IO.File]::Move($temporary, $Path, $true)
}

function Quote-WindowsArgument([string]$Value){
    if($Value.Length -gt 0 -and $Value -notmatch '[\s"]'){
        return $Value
    }
    $quoted = [Text.StringBuilder]::new()
    $null = $quoted.Append([char]34)
    $slashes = 0
    foreach($character in $Value.ToCharArray()){
        if($character -eq [char]92){
            $slashes++
            continue
        }
        if($character -eq [char]34){
            $null = $quoted.Append([char]92, 2 * $slashes + 1)
        }
        elseif($slashes -gt 0){
            $null = $quoted.Append([char]92, $slashes)
        }
        $null = $quoted.Append($character)
        $slashes = 0
    }
    if($slashes -gt 0){
        $null = $quoted.Append([char]92, 2 * $slashes)
    }
    $null = $quoted.Append([char]34)
    return $quoted.ToString()
}

$RepositoryRoot = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory, $RepositoryRoot)
$controlDirectory = $OutputDirectory + '-supervisor'
if(Test-Path -LiteralPath $controlDirectory){
    throw "Supervisor control directory already exists: $controlDirectory"
}
$null = New-Item -ItemType Directory -Path $controlDirectory
$statusPath = Join-Path $controlDirectory 'process-status.json'
$started = [DateTime]::UtcNow
$status = [ordered]@{
    state = 'starting'
    supervisor_pid = $PID
    supervisor_script = $PSCommandPath
    repository = $RepositoryRoot
    output_directory = $OutputDirectory
    started_utc = $started.ToString('o')
    heartbeat_utc = $started.ToString('o')
    probe_only = $ProbeOnly.IsPresent
    combines_partial_data = $false
    exit_code = $null
}
$child = $null
$exitStatus = 1
Write-StatusJson $statusPath $status
try{
    if($ProbeOnly){
        $status.state = 'probe_running'
        $status.probe_seconds = $ProbeSeconds
        Write-StatusJson (Join-Path $controlDirectory 'probe-start.json') $status
        Write-StatusJson $statusPath $status
        Start-Sleep -Seconds $ProbeSeconds
        $status.state = 'probe_completed'
        $status.exit_code = 0
        $exitStatus = 0
        Write-StatusJson (Join-Path $controlDirectory 'probe-completion.json') $status
    }
    else{
        if(Test-Path -LiteralPath $OutputDirectory){
            throw "A complete campaign requires a fresh output directory: $OutputDirectory"
        }
        if(!$FrozenArm -or !$ExpectedPlan){
            throw 'A real campaign requires explicit FrozenArm and ExpectedPlan paths.'
        }
        if(!$PythonExecutable){ $PythonExecutable = (Get-Command python.exe -ErrorAction Stop).Source }
        $FrozenArm = (Resolve-Path -LiteralPath $FrozenArm).Path
        $ExpectedPlan = (Resolve-Path -LiteralPath $ExpectedPlan).Path
        $PythonExecutable = (Resolve-Path -LiteralPath $PythonExecutable).Path
        $plan = Get-Content -LiteralPath $ExpectedPlan -Raw | ConvertFrom-Json -AsHashtable
        if($plan.blocks -ne 8 -or $plan.trial_count -ne 32 -or $plan.sample_intervals -ne 12 -or $plan.warmup_minimum_seconds -ne 30 -or $plan.order_seed -ne 0){
            throw 'Supervisor requires the original predeclared eight-block, 32-trial, fixed-warmup protocol.'
        }
        foreach($entry in $plan.runner_identity.GetEnumerator()){
            $source = Join-Path (Join-Path $RepositoryRoot 'tests/smoke') $entry.Key
            $actual = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
            if($actual -ne $entry.Value.sha256){ throw "Frozen acquisition script changed: $($entry.Key)" }
        }
        if([IO.Path]::GetFullPath($plan.executable) -ne (Join-Path $FrozenArm 'bin/refraction_smoke.exe') -or [IO.Path]::GetFullPath($plan.runtime) -ne (Join-Path $FrozenArm 'runtime')){
            throw 'Frozen arm paths differ from the original predeclaration.'
        }
        $arguments = @(
            '-B', (Join-Path $RepositoryRoot 'tests/smoke/refraction_query_profile_benchmark.py'),
            '--executable', (Join-Path $FrozenArm 'bin/refraction_smoke.exe'),
            '--runtime', (Join-Path $FrozenArm 'runtime'),
            '--logserver', (Join-Path $FrozenArm 'bin/logserver.exe'),
            '--source-manifest', (Join-Path $FrozenArm 'source-manifest.json'),
            '--output-directory', $OutputDirectory,
            '--blocks', '8', '--order-seed', '0', '--warmup-minimum-seconds', '30',
            '--warmup-intervals', [string]$plan.warmup_intervals,
            '--warmup-minimum-frames', [string]$plan.warmup_minimum_frames,
            '--sample-intervals', '12', '--minimum-frames', [string]$plan.minimum_measured_gpu_frames
        )
        $commandLine = ($arguments | ForEach-Object { Quote-WindowsArgument $_ }) -join ' '
        $stdout = Join-Path $controlDirectory 'stdout.log'
        $stderr = Join-Path $controlDirectory 'stderr.log'
        $status.python_executable = $PythonExecutable
        $status.arguments = $arguments
        $status.predeclared_plan = $ExpectedPlan
        $status.predeclared_plan_sha256 = (Get-FileHash -LiteralPath $ExpectedPlan -Algorithm SHA256).Hash.ToLowerInvariant()
        $status.stdout = $stdout
        $status.stderr = $stderr
        $status.state = 'launching'
        Write-StatusJson $statusPath $status
        $child = Start-Process -FilePath $PythonExecutable -ArgumentList $commandLine -WorkingDirectory $RepositoryRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        # Retain the native handle so ExitCode remains available after exit.
        $null = $child.Handle
        $status.python_pid = $child.Id
        $status.state = 'running'
        Write-StatusJson $statusPath $status
        while(!$child.WaitForExit(5000)){
            $status.heartbeat_utc = [DateTime]::UtcNow.ToString('o')
            Write-StatusJson $statusPath $status
        }
        $child.WaitForExit()
        $status.exit_code = $child.ExitCode
        $exitStatus = $child.ExitCode
        $status.state = if($exitStatus -eq 0){ 'completed' }else{ 'failed' }
    }
}
catch{
    $status.state = 'supervisor_failed'
    $status.error = $_.Exception.Message
    $status.exit_code = 1
    $exitStatus = 1
}
finally{
    $status.finished_utc = [DateTime]::UtcNow.ToString('o')
    $status.heartbeat_utc = $status.finished_utc
    $status.elapsed_seconds = ([DateTime]::UtcNow - $started).TotalSeconds
    Write-StatusJson $statusPath $status
    if($child){ $child.Dispose() }
}
exit $exitStatus
