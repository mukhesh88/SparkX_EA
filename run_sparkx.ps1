# SparkX Terminal // Laya SMC Algorithmic Engine Launcher
Set-Location -Path $PSScriptRoot
Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host "  SPARKX TERMINAL // ULTRA-LOW-LATENCY SMC DECISION ENGINE (LAYA-ONNX C++)" -ForegroundColor Cyan
Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host "Starting SparkX Desktop GUI (Backend auto-manages)..." -ForegroundColor Green
Start-Process -FilePath "build\cpp_engine\sparkx_terminal_gui.exe"
