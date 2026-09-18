param([string]$Exe, [string]$ShaderRoot, [string]$WorkRoot,
      [int]$Sets = 3, [switch]$Detailed, [switch]$SelfTest,
      [switch]$TraceAll, [int]$DumpFrame = 0)
$ErrorActionPreference = 'Stop'
function Read-Records([string[]]$Lines, [int]$ExpectedFrames = 48) {
    $records = @{}
    foreach ($line in $Lines) {
        if ($line -match '^\[Frame\] case=(\d+) index=(\d+) ') {
            $key = "Frame/$($Matches[1])/$($Matches[2])"
        } elseif ($line -match '^\[Resource\] frame=(\d+) name=(\w+) ') {
            $key = "Resource/$($Matches[1])/$($Matches[2])"
        } elseif ($line -match '^\[State\] frame=(\d+) ') {
            $key = "State/$($Matches[1])"
        } elseif ($line -match '^\[Shader\] name=(\S+) entry=(\S+) ') {
            $key = "Shader/$($Matches[1])/$($Matches[2])"
            if ($line -notmatch ' executable=[0-9a-f]+ valid=1$') { throw 'Missing executable provenance' }
            $line = $line -replace ' dxbc=[0-9a-f]+ ', ' dxbc=raw-recorded-in-log '
        } elseif ($line -match '^\[Environment\]') {
            $key = 'Environment'
        } elseif ($line -match '^\[Compiler\]') {
            $key = 'Compiler'
        } else { continue }
        if ($records.ContainsKey($key)) { throw "Duplicate record: $key" }
        if ($line -match 'valid=0') { throw "Capture failed: $line" }
        $records[$key] = $line
    }
    if (@($records.Keys | Where-Object { $_ -like 'Frame/*' }).Count -ne $ExpectedFrames) { throw 'Missing frames' }
    if ($ExpectedFrames -eq 48 -and @($records.Keys | Where-Object { $_ -like 'Shader/*' }).Count -ne 15) { throw 'Missing shader provenance' }
    if ($ExpectedFrames -eq 48 -and (!$records.ContainsKey('Compiler') -or !$records.ContainsKey('Environment'))) { throw 'Missing environment provenance' }
    for ($i = 1; $i -le $ExpectedFrames; ++$i) {
        foreach ($name in @('Weather', 'HDR', 'Tone')) {
            if (!$records.ContainsKey("Resource/$i/$name")) { throw "Missing resource $i/$name" }
        }
        if ($ExpectedFrames -eq 48 -and !$records.ContainsKey("State/$i")) { throw "Missing CPU state $i" }
        $case = [int][math]::Floor(($i - 1) / 4); $index = ($i - 1) % 4
        if (!$records.ContainsKey("Frame/$case/$index")) { throw "Missing case/index $case/$index" }
    }
    return $records
}
function Compare-Records($Reference, $Candidate) {
    # Detailed 모드의 추가 리소스는 허용하되 최소 계측의 모든 기록은 같아야 한다.
    $differences = @($Reference.Keys | Sort-Object | Where-Object {
        !$Candidate.ContainsKey($_) -or $Reference[$_] -cne $Candidate[$_]
    })
    if ($differences.Count) {
        foreach ($key in ($differences | Select-Object -First 8)) {
            Write-Host "DIFF $key`n  $($Reference[$key])`n  $($Candidate[$key])"
        }
        throw "Determinism mismatch: $($differences.Count) records"
    }
}
if ($SelfTest) {
    $fixture = @('[Frame] case=0 index=0 time=0 valid=1')
    foreach ($name in @('Weather','HDR','Tone')) { $fixture += "[Resource] frame=1 name=$name valid=1 hash=1234" }
    $good = Read-Records $fixture 1
    Compare-Records $good $good
    $caught = 0
    foreach ($bad in @(@($fixture[1..3]), @($fixture + $fixture[0]), @($fixture -replace 'valid=1','valid=0'))) {
        try { $null = Read-Records $bad 1 } catch { $caught++ }
    }
    try { Compare-Records $good (Read-Records ($fixture -replace '1234','1235') 1) } catch { $caught++ }
    if ($caught -ne 4) { throw "Comparator negative tests failed: $caught/4" }
    Write-Host 'DETERMINISM_COMPARATOR=PASS'; exit 0
}
if (!$Exe -or !$ShaderRoot -or !$WorkRoot -or $Sets -lt 1) { throw 'Missing test arguments' }
# A unique directory preserves all previous caches and evidence.
$root = Join-Path ([IO.Path]::GetFullPath($WorkRoot)) ([guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $root
$shaders = Join-Path $root 'shaders'
Copy-Item -LiteralPath $ShaderRoot -Destination $shaders -Recurse
$envNames = @('VCLOUD_SHADER_OVERRIDE_DIR','VCLOUD_TEST_SHADER_CACHE','VCLOUD_TEST_DUMP_FRAME','VCLOUD_TEST_DUMP_ROOT')
$previous = @{}; foreach ($name in $envNames) { $previous[$name] = [Environment]::GetEnvironmentVariable($name) }
try {
    $env:VCLOUD_SHADER_OVERRIDE_DIR = $shaders
    $baseline = $null
    for ($set = 0; $set -lt $Sets; ++$set) {
        $cache = Join-Path $root "cache-$set"
        $null = New-Item -ItemType Directory -Path $cache
        $env:VCLOUD_TEST_SHADER_CACHE = $cache
        $runs = if ($Detailed) { 4 } else { 3 }
        for ($run = 0; $run -lt $runs; ++$run) {
            $log = Join-Path $root "set-$set-run-$run.log"
            $arguments = @('--determinism-smoke-test')
            if ($run -eq 3 -or $TraceAll) { $arguments += '--determinism-detailed' }
            $env:VCLOUD_TEST_DUMP_FRAME = "$DumpFrame"
            $env:VCLOUD_TEST_DUMP_ROOT = Join-Path $root "set-$set-run-$run-dump"
            $process = Start-Process -FilePath $Exe -ArgumentList $arguments -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput $log -RedirectStandardError "$log.stderr"
            $null = $process.Handle
            if (!$process.WaitForExit(180000)) { $process.Kill(); throw "Timeout: $log" }
            $lines = Get-Content -LiteralPath $log
            if ($lines -notcontains '[BuildPolicy] strict_validation=1') {
                throw 'Bit-exact matrix requires VCLOUD_STRICT_VALIDATION=ON'
            }
            if ($process.ExitCode -ne 0 -or $lines -notcontains 'DETERMINISM_SMOKE=PASS') { throw "Process failure: $log" }
            $counter = @($lines | Where-Object { $_ -match '^\[Cache\] compile=(\d+) hits=(\d+)$' })
            if ($counter.Count -ne 1 -or $counter[0] -notmatch 'compile=(\d+) hits=(\d+)') { throw 'Missing cache counters' }
            if (($run -eq 0 -and ([int]$Matches[1] -eq 0 -or [int]$Matches[2] -ne 0)) -or
                ($run -gt 0 -and ([int]$Matches[1] -ne 0 -or [int]$Matches[2] -eq 0))) { throw "Wrong cold/warm state: $counter" }
            $records = Read-Records $lines
            Write-Host "Captured set=$set run=$run $log"
            if ($null -eq $baseline) { $baseline = $records }
            else { Compare-Records $baseline $records }
        }
    }
    $transitionLog = Join-Path $root 'transitions.log'
    $process = Start-Process -FilePath $Exe -ArgumentList @('--determinism-smoke-test','--determinism-state-test') `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput $transitionLog -RedirectStandardError "$transitionLog.stderr"
    $null = $process.Handle
    if (!$process.WaitForExit(180000)) { $process.Kill(); throw 'Transition timeout' }
    if ($process.ExitCode -ne 0 -or (Get-Content $transitionLog) -notcontains 'DETERMINISM_TRANSITIONS=PASS') {
        throw "State transition regression: $transitionLog"
    }
    Write-Host "DETERMINISM_MATRIX=PASS sets=$Sets root=$root"
} finally {
    foreach ($name in $envNames) { [Environment]::SetEnvironmentVariable($name, $previous[$name]) }
}
