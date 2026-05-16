@echo off
REM Lab2+: build через MSBuild (как Visual Studio). Надёжнее, чем ручной cl для другой структуры каталогов.
REM NuGet: packages.config + классический .vcxproj не поддерживают MSBuild /restore (MSB4057).
REM Если папки packages\... нет: nuget restore packages.config -PackagesDirectory packages
setlocal EnableExtensions EnableDelayedExpansion

set "CFG=%~1"
set "PLATFORM=%~2"

if "%CFG%"=="" set "CFG=Release"
if "%PLATFORM%"=="" set "PLATFORM=x64"

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
  echo ERROR: MSBuild.exe not found. Install VS 2022 with "Desktop development with C++".
  echo.
  echo Нажмите любую клавишу, чтобы закрыть окно...
  pause >nul
  exit /b 1
)

REM MSB4019: MSBuild есть, но не установлена нагрузка "Разработка классических приложений на C++"
set "MSBUILD_BIN="
for %%I in ("%MSBUILD%") do set "MSBUILD_BIN=%%~dpI"
set "CPP_DEFAULT_PROPS=!MSBUILD_BIN!..\..\Microsoft\VC\v170\Microsoft.Cpp.Default.props"
if not exist "!CPP_DEFAULT_PROPS!" (
  echo.
  echo ERROR: не найден C++ toolset для MSBuild ^(ожидался файл^):
  echo   !CPP_DEFAULT_PROPS!
  echo.
  echo Установите в Visual Studio Installer для VS 2022 рабочую нагрузку:
  echo   "Разработка классических приложений на C++" ^(Desktop development with C++^)
  echo В правой колонке отметьте хотя бы: MSVC v143, Windows 10/11 SDK, C++ CMake tools ^(по желанию^).
  echo.
  echo Нажмите любую клавишу, чтобы закрыть окно...
  pause >nul
  exit /b 1
)

REM В репозитории часто лежит только include\ DirectXTex; без native\*.lib линковка не соберётся.
set "DXTEX_LIB=%~dp0packages\directxtex_desktop_win10.2025.10.28.1\native\lib\x64\%CFG%\DirectXTex.lib"
if not exist "%DXTEX_LIB%" (
  echo.
  echo DirectXTex.lib не найден — восстанавливаю NuGet-пакеты ...
  call "%~dp0restore_packages.bat"
  if errorlevel 1 (
    echo.
    echo ERROR: restore_packages.bat завершился с ошибкой.
    echo.
    echo Нажмите любую клавишу, чтобы закрыть окно...
    pause >nul
    exit /b 1
  )
)
if not exist "%DXTEX_LIB%" (
  echo.
  echo ERROR: после восстановления всё ещё нет файла:
  echo   %DXTEX_LIB%
  echo Запустите вручную: %~dp0restore_packages.bat
  echo Либо в Visual Studio: ПКМ по решению — Restore NuGet Packages.
  echo.
  echo Нажмите любую клавишу, чтобы закрыть окно...
  pause >nul
  exit /b 1
)

echo ========================================
echo DirectX12CustomLib  %PLATFORM%^| %CFG%
echo MSBuild: %MSBUILD%
echo ========================================
echo.

"%MSBUILD%" "%~dp0DirectX12CustomLib.sln" /t:Rebuild /p:Configuration=%CFG% /p:Platform=%PLATFORM% /v:m
if errorlevel 1 (
  echo.
  echo BUILD FAILED
  echo.
  echo Нажмите любую клавишу, чтобы закрыть окно...
  pause >nul
  exit /b 1
)

echo.
echo OK: %~dp0bin\%PLATFORM%\%CFG%\DirectX12CustomLib.exe
exit /b 0
