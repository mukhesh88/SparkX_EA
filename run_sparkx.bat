@echo off
title SparkX EA // Laya SMC Algorithmic Engine
cd /d "%~dp0"
echo ================================================================================
echo   SPARKX EA // ULTRA-LOW-LATENCY SMC DECISION ENGINE (LAYA-ONNX C++)
echo ================================================================================
echo Starting SparkX EA Standalone Executable...
if exist "SparkX_EA.exe" (
    start "" "SparkX_EA.exe"
) else if exist "SparkX_Ai_Algo.exe" (
    start "" "SparkX_Ai_Algo.exe"
) else (
    start "" "build\cpp_engine\SparkX_EA.exe"
)
exit /b 0
