# SparkX Terminal Distribution Packager
param(
    [switch]$Zip = $false
)

$RootDir = Split-Path -Parent $PSScriptRoot
$ReleaseDir = Join-Path $RootDir "release\SparkX_Terminal"

Write-Host "Creating SparkX Standalone Release Package in: $ReleaseDir" -ForegroundColor Cyan

if (Test-Path $ReleaseDir) {
    Remove-Item -Recurse -Force $ReleaseDir
}
New-Item -ItemType Directory -Force -Path $ReleaseDir | Out-Null

# 1. Main C++ Executable & Runtime DLL
Copy-Item (Join-Path $RootDir "SparkX_Ai_Algo.exe") -Destination $ReleaseDir
Copy-Item (Join-Path $RootDir "onnxruntime.dll") -Destination $ReleaseDir
Copy-Item (Join-Path $RootDir "run_sparkx.bat") -Destination $ReleaseDir

# 2. Assets (Lemon Milk Fonts)
$DestAssets = Join-Path $ReleaseDir "assets\fonts"
New-Item -ItemType Directory -Force -Path $DestAssets | Out-Null
Copy-Item (Join-Path $RootDir "assets\fonts\*") -Destination $DestAssets -Recurse

# 3. Neural Network Models (Laya ONNX & Tokenizer)
$DestModels = Join-Path $ReleaseDir "models"
New-Item -ItemType Directory -Force -Path $DestModels | Out-Null
Copy-Item (Join-Path $RootDir "models\laya.onnx") -Destination $DestModels
if (Test-Path (Join-Path $RootDir "models\laya.onnx.data")) {
    Write-Host "Copying Laya ONNX weights (1.58 GB)..." -ForegroundColor Yellow
    Copy-Item (Join-Path $RootDir "models\laya.onnx.data") -Destination $DestModels
}
Copy-Item (Join-Path $RootDir "models\tokenizer") -Destination $DestModels -Recurse

# 4. Configuration & Mobile Alerts
$DestConfig = Join-Path $ReleaseDir "config"
New-Item -ItemType Directory -Force -Path $DestConfig | Out-Null
Copy-Item (Join-Path $RootDir "config\*") -Destination $DestConfig -Recurse

# 5. Standalone Backend
$DestBackend = Join-Path $ReleaseDir "dist\SparkX_Backend"
New-Item -ItemType Directory -Force -Path $DestBackend | Out-Null
Copy-Item (Join-Path $RootDir "dist\SparkX_Backend\*") -Destination $DestBackend -Recurse

# 6. Instructions README
$ReadmeContent = @"
================================================================================
  SPARKX TERMINAL // ULTRA-LOW-LATENCY SMC DECISION ENGINE (LAYA-ONNX C++)
================================================================================

HOW TO RUN:
1. Double-click 'SparkX_Ai_Algo.exe' or 'run_sparkx.bat'.
2. The engine will automatically initialize the Laya ONNX neural network and 
   silently start the market data processor in the background.

REQUIREMENTS:
- Live Broker Execution: Open your MetaTrader 5 desktop terminal and log in.
- Offline / Simulation: No MT5 required; automatically runs the built-in 
  institutional synthetic market simulator.
"@
Set-Content -Path (Join-Path $ReleaseDir "README.txt") -Value $ReadmeContent

Write-Host "Release package successfully created at: $ReleaseDir" -ForegroundColor Green

if ($Zip) {
    $ZipFile = Join-Path $RootDir "release\SparkX_Terminal_v1.0.zip"
    Write-Host "Compressing to $ZipFile..." -ForegroundColor Cyan
    Compress-Archive -Path "$ReleaseDir\*" -DestinationPath $ZipFile -Force
    Write-Host "Zip archive created: $ZipFile" -ForegroundColor Green
}
