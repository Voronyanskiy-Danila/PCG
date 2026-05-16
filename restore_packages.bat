@echo off
REM Восстановление NuGet (packages.config) без nuget в PATH:
REM 1) NuGet.exe рядом с VS, если есть; 2) иначе tools\nuget.exe (скачивается один раз).
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

REM Если в git был только include\, restore не перезапишет папку — удаляем неполный пакет.
set "DXTEX_DIR=%~dp0packages\directxtex_desktop_win10.2025.10.28.1"
if exist "%DXTEX_DIR%" if not exist "%DXTEX_DIR%\native\lib\x64\Release\DirectXTex.lib" (
  echo Удаляю неполную папку DirectXTex, чтобы NuGet скачал полный пакет...
  rmdir /s /q "%DXTEX_DIR%"
)

set "NUGET_EXE="
for %%P in (
  "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\NuGet.exe"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\IDE\NuGet.exe"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\NuGet.exe"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\Common7\IDE\NuGet.exe"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional\Common7\IDE\NuGet.exe"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\NuGet.exe"
) do if exist %%~P if not defined NUGET_EXE set "NUGET_EXE=%%~P"

if not defined NUGET_EXE if exist "%~dp0tools\nuget.exe" set "NUGET_EXE=%~dp0tools\nuget.exe"

if not defined NUGET_EXE (
  echo NuGet.exe не найден в Visual Studio. Скачиваю официальный CLI в tools\nuget.exe ...
  if not exist "%~dp0tools" mkdir "%~dp0tools"
  powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$u='https://dist.nuget.org/win-x86-commandline/latest/nuget.exe'; $o='%~dp0tools\nuget.exe'; try { Invoke-WebRequest -Uri $u -OutFile $o -UseBasicParsing } catch { exit 1 }"
  if errorlevel 1 (
    echo ERROR: не удалось скачать nuget.exe. Проверьте интернет или скачайте вручную:
    echo   https://dist.nuget.org/win-x86-commandline/latest/nuget.exe
    echo и положите файл как: %~dp0tools\nuget.exe
    exit /b 1
  )
  set "NUGET_EXE=%~dp0tools\nuget.exe"
)

echo Используется: !NUGET_EXE!
echo.
"!NUGET_EXE!" restore "%~dp0packages.config" -PackagesDirectory "%~dp0packages"
set "ERR=!errorlevel!"
if not "!ERR!"=="0" exit /b !ERR!

echo.
echo OK: пакеты восстановлены в %~dp0packages
exit /b 0
