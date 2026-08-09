param(
    [string]$Executable = '.\build\Release\VolumetricCloud.exe',
    [string]$ShaderRoot = '.\build\Release\shaders',
    [string]$OutputRoot = '.\captures\performance\stage9'
)
$ErrorActionPreference = 'Stop'
$executablePath = (Resolve-Path -LiteralPath $Executable).Path
$shaderPath = (Resolve-Path -LiteralPath $ShaderRoot).Path
$baseline = Join-Path $OutputRoot 'baseline'
$optimized = Join-Path $OutputRoot 'optimized'

# Windows GUI 실행 파일은 PowerShell의 & 호출이 프로세스 종료를 기다리지 않을 수 있다.
# Start-Process -Wait로 두 측정이 절대 겹치지 않게 한다.
$baselineArguments = @('--stage9-benchmark','--scenario','all','--optimization','off','--output',$baseline,'--shader-root',$shaderPath)
$optimizedArguments = @('--stage9-benchmark','--scenario','all','--optimization','balanced','--output',$optimized,'--shader-root',$shaderPath)
$baselineProcess = Start-Process -FilePath $executablePath -ArgumentList $baselineArguments -PassThru -Wait
if ($baselineProcess.ExitCode -ne 0) { throw "Baseline benchmark failed: $($baselineProcess.ExitCode)" }
$optimizedProcess = Start-Process -FilePath $executablePath -ArgumentList $optimizedArguments -PassThru -Wait
if ($optimizedProcess.ExitCode -ne 0) { throw "Optimized benchmark failed: $($optimizedProcess.ExitCode)" }

& (Join-Path $PSScriptRoot 'Compare-Stage9Performance.ps1') `
    -Baseline (Join-Path $baseline 'summary.json') `
    -Optimized (Join-Path $optimized 'summary.json') `
    -Output (Join-Path $OutputRoot 'comparison')
