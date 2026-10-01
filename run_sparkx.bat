@echo off
title SparkX Terminal // Laya SMC Algorithmic Engine
cd /d "%~dp0"
echo ================================================================================
echo   SPARKX TERMINAL // ULTRA-LOW-LATENCY SMC DECISION ENGINE (LAYA-ONNX C++)
echo ================================================================================
echo Starting SparkX Standalone Executable...
if exist "SparkX_Ai_Algo.exe" (
    start "" "SparkX_Ai_Algo.exe"
) else (
    start "" "build\cpp_engine\SparkX_Ai_Algo.exe"
)
exit /b 0
