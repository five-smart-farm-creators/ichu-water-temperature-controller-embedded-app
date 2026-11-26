param(
    [string]$ArduinoCli,
    [string]$ConfigFile
)

$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot
$outputDir = Join-Path $repoRoot 'build'
$localConfigPath = Join-Path $outputDir 'arduino-local.json'

# Machine-specific tool paths stay in the ignored build directory.
if (Test-Path -LiteralPath $localConfigPath -PathType Leaf) {
    $localConfig = Get-Content -LiteralPath $localConfigPath -Raw | ConvertFrom-Json
    if (-not $PSBoundParameters.ContainsKey('ArduinoCli')) {
        $ArduinoCli = $localConfig.ArduinoCli
    }
    if (-not $PSBoundParameters.ContainsKey('ConfigFile')) {
        $ConfigFile = $localConfig.ConfigFile
    }
}
if ([string]::IsNullOrWhiteSpace($ArduinoCli)) {
    $ArduinoCli = 'arduino-cli'
}
$compiler = Get-Command -Name $ArduinoCli -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($null -eq $compiler) {
    throw 'Arduino CLI was not found. Install it on PATH or pass -ArduinoCli with its executable path.'
}

$compileArgs = @()
if (-not [string]::IsNullOrWhiteSpace($ConfigFile)) {
    if (-not [IO.Path]::IsPathRooted($ConfigFile)) {
        $ConfigFile = Join-Path $repoRoot $ConfigFile
    }
    if (-not (Test-Path -LiteralPath $ConfigFile -PathType Leaf)) {
        throw "Arduino configuration was not found: $ConfigFile"
    }
    $compileArgs += @('--config-file', $ConfigFile)
}

# Arduino requires sketch.ino to live in a folder named sketch. Exclude native tests.
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar)
$compileRoot = Join-Path $tempRoot ('ichu-water-build-' + [Guid]::NewGuid().ToString('N'))
$sketchDir = Join-Path $compileRoot 'sketch'
$artifactDir = Join-Path $compileRoot 'output'
try {
    New-Item -ItemType Directory -Path $sketchDir, $artifactDir -Force | Out-Null
    Get-ChildItem -LiteralPath $repoRoot -File |
        Where-Object { $_.Extension -in @('.ino', '.h', '.cpp') } |
        Copy-Item -Destination $sketchDir

    $compileArgs += @('compile', '--fqbn', 'esp32:esp32:esp32', '--warnings', 'all',
                      '--output-dir', $artifactDir, $sketchDir)
    & $compiler.Source @compileArgs
    if ($LASTEXITCODE -ne 0) {
        throw "ESP32 compilation failed (exit $LASTEXITCODE)."
    }
    # Validate fresh output before replacing the firmware selected by Wokwi.
    foreach ($artifact in @('sketch.ino.bin', 'sketch.ino.elf')) {
        $artifactPath = Join-Path $artifactDir $artifact
        if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf) -or
            (Get-Item -LiteralPath $artifactPath).Length -eq 0) {
            throw "The compiler did not generate a non-empty $artifact."
        }
    }
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
    Get-ChildItem -LiteralPath $artifactDir -File | Copy-Item -Destination $outputDir -Force
    Write-Host "Wokwi firmware ready: $(Join-Path $outputDir 'sketch.ino.bin')"
    Write-Host "Wokwi ELF ready: $(Join-Path $outputDir 'sketch.ino.elf')"
} finally {
    # Remove only this invocation's temporary directory, after checking its absolute parent.
    $resolvedCompileRoot = [IO.Path]::GetFullPath($compileRoot)
    if ((Split-Path -Parent $resolvedCompileRoot) -eq $tempRoot -and
        (Test-Path -LiteralPath $resolvedCompileRoot -PathType Container)) {
        Remove-Item -LiteralPath $resolvedCompileRoot -Recurse -Force
    }
}
