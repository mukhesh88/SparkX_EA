# SparkX Terminal // Laya SMC Algorithmic Engine Launcher
Set-Location -Path $PSScriptRoot
Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host "  SPARKX TERMINAL // ULTRA-LOW-LATENCY SMC DECISION ENGINE (LAYA-ONNX C++)" -ForegroundColor Cyan
Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host "Starting SparkX Standalone Executable..." -ForegroundColor Green
if (Test-Path "SparkX_Ai_Algo.exe") {
    Start-Process -FilePath ".\SparkX_Ai_Algo.exe"
} else {
    Start-Process -FilePath ".\build\cpp_engine\SparkX_Ai_Algo.exe"
}
