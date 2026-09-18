param(
    [string]$Exe = "$PSScriptRoot/../build/Release/VolumetricCloud.exe",
    [string]$Root = "$PSScriptRoot/../captures/stage15-directional-lighting/00-baseline",
    [switch]$VerifyOnly
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath("$PSScriptRoot/..")
$captureRoot = (Resolve-Path -LiteralPath $Root).Path
$executable = (Resolve-Path -LiteralPath $Exe).Path
$shaderRoot = Join-Path $projectRoot 'shaders'
if (-not $VerifyOnly) {
    if (Test-Path -LiteralPath "$captureRoot/clean") { throw 'Existing baseline is immutable; use -VerifyOnly.' }
    $manifest = Get-Content -LiteralPath "$captureRoot/source-manifest.json" -Raw | ConvertFrom-Json
    foreach ($entry in $manifest) {
        if ($entry.path -like 'shaders/*') {
            if ((Get-FileHash -LiteralPath (Join-Path $projectRoot $entry.path) -Algorithm SHA256).Hash -ne $entry.sha256) {
                throw "Shader changed since baseline source archive: $($entry.path)"
            }
        }
    }
}
$logRoot = Join-Path $projectRoot ('build/directional-lighting/' + [guid]::NewGuid().ToString())
New-Item -ItemType Directory -Path $logRoot | Out-Null
$oldRoot = $env:VCLOUD_DIRECTIONAL_BASELINE_ROOT
$oldShaders = $env:VCLOUD_SHADER_OVERRIDE_DIR
try {
    $env:VCLOUD_DIRECTIONAL_BASELINE_ROOT = $captureRoot
    $env:VCLOUD_SHADER_OVERRIDE_DIR = $shaderRoot
    $argument = if ($VerifyOnly) { '--directional-lighting-verify' } else { '--directional-lighting-baseline' }
    $process = Start-Process -FilePath $executable -ArgumentList $argument -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput "$logRoot/stdout.txt" -RedirectStandardError "$logRoot/stderr.txt"
    $processHandle = $process.Handle
    $process.WaitForExit()
    Get-Content -LiteralPath "$logRoot/stdout.txt"
    Get-Content -LiteralPath "$logRoot/stderr.txt"
    if ($process.ExitCode -ne 0) { throw "Baseline runner failed ($($process.ExitCode)); logs: $logRoot" }
    if (-not $VerifyOnly) {
        $record = [ordered]@{
            executable = $executable
            executableSha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
            argument = $argument
            shaderRoot = $shaderRoot
            logRoot = $logRoot
            capturedAt = (Get-Date).ToString('o')
        } | ConvertTo-Json
        $recordPath = Join-Path $captureRoot 'runner.json'
        $stream = [IO.File]::Open($recordPath, [IO.FileMode]::CreateNew)
        try {
            $bytes = [Text.Encoding]::UTF8.GetBytes($record)
            $stream.Write($bytes, 0, $bytes.Length)
        } finally { $stream.Dispose() }
    }
    Write-Output "Logs: $logRoot"
} finally {
    $env:VCLOUD_DIRECTIONAL_BASELINE_ROOT = $oldRoot
    $env:VCLOUD_SHADER_OVERRIDE_DIR = $oldShaders
}
