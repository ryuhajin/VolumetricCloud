param(
    [Parameter(Mandatory=$true)][string]$Baseline,
    [Parameter(Mandatory=$true)][string]$Optimized,
    [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference = 'Stop'
$baselineData = Get-Content -LiteralPath $Baseline -Raw | ConvertFrom-Json
$optimizedData = Get-Content -LiteralPath $Optimized -Raw | ConvertFrom-Json
$rows = @()
foreach ($baseScene in $baselineData.scenarios) {
    $optimizedScene = $optimizedData.scenarios | Where-Object name -eq $baseScene.name | Select-Object -First 1
    if ($null -eq $optimizedScene) { throw "Missing scenario: $($baseScene.name)" }
    $baseP95 = [double]$baseScene.gpuCloud.p95
    $optimizedP95 = [double]$optimizedScene.gpuCloud.p95
    $improvement = if ($baseP95 -gt 0) { 100.0 * ($baseP95 - $optimizedP95) / $baseP95 } else { 0.0 }
    $limit = if ($baseScene.name -eq 'GroundHorizonDense') { 15.0 } else { -3.0 }
    $rows += [pscustomobject]@{
        scenario = $baseScene.name
        baselineP50 = [double]$baseScene.gpuCloud.p50
        optimizedP50 = [double]$optimizedScene.gpuCloud.p50
        baselineP95 = $baseP95
        optimizedP95 = $optimizedP95
        improvementPercent = $improvement
        passed = $improvement -ge $limit
    }
}
$outputDirectory = Split-Path -Parent $Output
if ($outputDirectory) { New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null }
$rows | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($Output + '.json') -Encoding utf8
$lines = @('# Stage 8 알고리즘 / Stage 9 Balanced 성능 비교', '', '| Scenario | Baseline p50 | Optimized p50 | Baseline p95 | Optimized p95 | 개선율 | 판정 |', '|---|---:|---:|---:|---:|---:|---|')
foreach ($row in $rows) {
    $verdict = if ($row.passed) { 'PASS' } else { 'FAIL' }
    $lines += "| $($row.scenario) | $($row.baselineP50.ToString('F3')) | $($row.optimizedP50.ToString('F3')) | $($row.baselineP95.ToString('F3')) | $($row.optimizedP95.ToString('F3')) | $($row.improvementPercent.ToString('F2'))% | $verdict |"
}
$lines += '', 'GroundHorizonDense는 15% 이상 개선, 나머지는 3% 초과 회귀 없음이 기준이다.'
$lines | Set-Content -LiteralPath ($Output + '.md') -Encoding utf8
if (@($rows | Where-Object { -not $_.passed }).Count -gt 0) { exit 2 }
