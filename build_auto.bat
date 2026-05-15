@echo off
setlocal EnableExtensions
REM Build via MSBuild (same as VS). Avoids fragile "echo cl.exe ... " temp scripts.

set "CFG=Release"
set "PLATFORM=x64"

set "MSBUILD="
for %%E in (
  "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
) do if exist %%~E set "MSBUILD=%%~E"

if "%MSBUILD%"=="" (
  echo ERROR: MSBuild.exe not found. Install Visual Studio 2022 with "Desktop development with C++".
  exit /b 1
)

echo ========================================
echo DirectX12CustomLib — %PLATFORM% ^| %CFG%
echo MSBuild: %MSBUILD%
echo ========================================
echo.

"%MSBUILD%" "%~dp0DirectX12CustomLib.sln" /restore /t:Rebuild /p:Configuration=%CFG% /p:Platform=%PLATFORM% /v:m
if errorlevel 1 (
  echo.
  echo BUILD FAILED
  exit /b 1
)

echo.
echo OK: %~dp0bin\%PLATFORM%\%CFG%\DirectX12CustomLib.exe
exit /b 0
