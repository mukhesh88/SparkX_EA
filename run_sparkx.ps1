# SparkX EA // Laya SMC Algorithmic Engine Launcher
Set-Location -Path $PSScriptRoot
Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host "  SPARKX EA // ULTRA-LOW-LATENCY SMC DECISION ENGINE (LAYA-ONNX C++)" -ForegroundColor Cyan
Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host "Starting SparkX EA Standalone Executable..." -ForegroundColor Green
if (Test-Path "SparkX_EA.exe") {
    Start-Process -FilePath ".\SparkX_EA.exe"
} elseif (Test-Path "SparkX_Ai_Algo.exe") {
    Start-Process -FilePath ".\SparkX_Ai_Algo.exe"
} else {
    Start-Process -FilePath ".\build\cpp_engine\SparkX_EA.exe"
}
