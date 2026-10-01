@echo off
title SparkX Terminal // Laya SMC Algorithmic Engine
cd /d "%~dp0"
echo ================================================================================
echo   SPARKX TERMINAL // ULTRA-LOW-LATENCY SMC DECISION ENGINE (LAYA-ONNX C++)
echo ================================================================================
echo Starting SparkX Desktop Engine...
start "" "build\cpp_engine\sparkx_terminal_gui.exe"
exit /b 0
