@echo off
setlocal enabledelayedexpansion
REM Автоматическая сборка с автонастройкой окружения Visual Studio

echo ========================================
echo Building DirectX12CustomLib
echo ========================================
echo.

REM Поиск vcvarsall.bat
set "VCVARS="

REM VS 2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    set "VS_VERSION=2022 Community"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
    set "VS_VERSION=2022 Professional"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
    set "VS_VERSION=2022 Enterprise"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    set "VS_VERSION=2022 Community"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
    set "VS_VERSION=2022 Professional"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
    set "VS_VERSION=2022 Enterprise"
)

REM VS 2019
if "!VCVARS!"=="" (
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
        set "VS_VERSION=2019 Community"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
        set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat"
        set "VS_VERSION=2019 Professional"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
        set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
        set "VS_VERSION=2019 Enterprise"
    )
)

if "!VCVARS!"=="" (
    echo ERROR: Visual Studio не найдена!
    echo.
    echo Пожалуйста, установите Visual Studio 2019 или 2022 с компонентами:
    echo   - Desktop development with C++
    echo   - Windows 10/11 SDK
    echo.
    pause
    exit /b 1
)

echo Найдена Visual Studio: !VS_VERSION!
echo Настраиваю окружение...
echo.

REM Создание папок
if not exist "bin\x64\Release" mkdir "bin\x64\Release"
if not exist "obj\x64\Release" mkdir "obj\x64\Release"

REM Создание временного скрипта компиляции
set "BUILD_SCRIPT=%TEMP%\build_%RANDOM%.bat"
set "CURRENT_DIR=%CD%"

(
    echo @echo off
    echo call "!VCVARS!" x64
    echo cd /d "!CURRENT_DIR!"
    echo echo Compiling source files...
    echo cl.exe /nologo /W4 /permissive- /utf-8 /std:c++20 /EHsc /O2 /DNDEBUG /DUNICODE /D_UNICODE ^
    echo     /I. ^
    echo     /Fobin\x64\Release\ ^
    echo     /Foobj\x64\Release\ ^
    echo     /Fdbin\x64\Release\DirectX12CustomLib.pdb ^
    echo     src\app\AppBase.cpp ^
    echo     src\app\CubeApp.cpp ^
    echo     src\core\FrameTimer.cpp ^
    echo     src\graphics\Dx12Utils.cpp ^
    echo     src\math\MathUtils.cpp ^
    echo     src\math\MeshGenerator.cpp ^
    echo     src\resources\TextureLoaderDDS.cpp ^
    echo     src\scene\CameraComponent.cpp ^
    echo     /link /SUBSYSTEM:WINDOWS /OUT:bin\x64\Release\DirectX12CustomLib.exe ^
    echo     d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib winmm.lib
    echo if %%ERRORLEVEL%% NEQ 0 exit /b 1
    echo echo.
    echo echo Copying content folder...
    echo if not exist "bin\x64\Release\content" mkdir "bin\x64\Release\content"
    echo xcopy /E /I /Y "content\*" "bin\x64\Release\content\" ^>nul
) > "!BUILD_SCRIPT!"

REM Выполнение скрипта компиляции
call "!BUILD_SCRIPT!"
set "BUILD_RESULT=!ERRORLEVEL!"

REM Удаление временного скрипта
if exist "!BUILD_SCRIPT!" del "!BUILD_SCRIPT!"

if !BUILD_RESULT! NEQ 0 (
    echo.
    echo ERROR: Компиляция не удалась!
    pause
    exit /b 1
)

echo.
echo ========================================
echo Build completed successfully!
echo Executable: bin\x64\Release\DirectX12CustomLib.exe
echo ========================================
echo.
pause

