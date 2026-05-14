@echo off
REM Auto-build: locate Visual Studio, restore NuGet if needed, compile with cl.exe, copy content.
REM All user-facing text is ASCII so the console shows no mojibake regardless of code page.

setlocal enabledelayedexpansion
REM Prefer English tool messages (cl/link) when MSVC is installed with multiple locales.
set "VSLANG=1033"
chcp 65001 >nul

echo ========================================
echo Building DirectX12CustomLib
echo ========================================
echo.

REM Find vcvarsall.bat
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
    echo ERROR: Visual Studio not found.
    echo.
    echo Install VS 2019 or 2022 with:
    echo   - Desktop development with C++
    echo   - Windows 10/11 SDK
    echo.
    echo Press any key to exit.
    pause >nul
    exit /b 1
)

echo Found Visual Studio: !VS_VERSION!
echo Initializing MSVC environment...
echo.

REM NuGet: DirectXTex (same package as kg26-27 lab)
set "DXTEX_INC=%~dp0packages\directxtex_desktop_win10.2025.10.28.1\include"
set "DXTEX_LIB=%~dp0packages\directxtex_desktop_win10.2025.10.28.1\native\lib\x64\Release"
if not exist "!DXTEX_INC!\DirectXTex.h" (
    if exist "%~dp0nuget.exe" (
        echo Restoring NuGet package directxtex_desktop_win10...
        "%~dp0nuget.exe" restore "%~dp0packages.config" -PackagesDirectory "%~dp0packages"
    )
)
if not exist "!DXTEX_INC!\DirectXTex.h" (
    echo ERROR: DirectXTex headers not found. Run:
    echo   nuget restore packages.config -PackagesDirectory packages
    echo Press any key to exit.
    pause >nul
    exit /b 1
)

if not exist "bin\x64\Release" mkdir "bin\x64\Release"
if not exist "obj\x64\Release" mkdir "obj\x64\Release"

REM Build a temp .bat line-by-line. Do not use ( echo ... ^ ... ) blocks: line continuation
REM merges with the next echo and breaks the cl.exe command line.
set "BUILD_SCRIPT=%TEMP%\build_%RANDOM%.bat"
set "CURRENT_DIR=%CD%"

> "!BUILD_SCRIPT!" echo @echo off
>> "!BUILD_SCRIPT!" echo set VSLANG=1033
>> "!BUILD_SCRIPT!" echo call "!VCVARS!" x64
>> "!BUILD_SCRIPT!" echo chcp 65001 ^>nul
>> "!BUILD_SCRIPT!" echo cd /d "!CURRENT_DIR!"
>> "!BUILD_SCRIPT!" echo echo Compiling...
REM Single /Fo avoids cl D9025 (duplicate object output directory).
>> "!BUILD_SCRIPT!" echo cl.exe /nologo /W4 /permissive- /utf-8 /std:c++20 /EHsc /O2 /MD /DNDEBUG /DUNICODE /D_UNICODE /DHAS_DIRECTXTEX /I. /I"!DXTEX_INC!" /Foobj\x64\Release\ /Fdbin\x64\Release\DirectX12CustomLib.pdb src\application\AppBase.cpp src\application\CubeApp.cpp src\engine\FrameTimer.cpp src\engine\dx12\Dx12Utils.cpp src\math\MathUtils.cpp src\loaders\ObjMtlLoader.cpp src\loaders\TextureLoaderDirectXTex.cpp /link /SUBSYSTEM:WINDOWS /OUT:bin\x64\Release\DirectX12CustomLib.exe /LIBPATH:"!DXTEX_LIB!" DirectXTex.lib d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib winmm.lib windowscodecs.lib ole32.lib user32.lib gdi32.lib
>> "!BUILD_SCRIPT!" echo if %%ERRORLEVEL%% NEQ 0 exit /b 1
>> "!BUILD_SCRIPT!" echo echo.
>> "!BUILD_SCRIPT!" echo echo Copying content to bin\x64\Release\content ...
>> "!BUILD_SCRIPT!" echo if not exist "bin\x64\Release\content" mkdir "bin\x64\Release\content"
>> "!BUILD_SCRIPT!" echo xcopy /E /I /Y "content\*" "bin\x64\Release\content\" ^>nul

call "!BUILD_SCRIPT!"
set "BUILD_RESULT=!ERRORLEVEL!"

if exist "!BUILD_SCRIPT!" del "!BUILD_SCRIPT!"

if !BUILD_RESULT! NEQ 0 (
    echo.
    echo ERROR: Build failed.
    echo Press any key to exit.
    pause >nul
    exit /b 1
)

echo.
echo ========================================
echo Build completed successfully.
echo Output: bin\x64\Release\DirectX12CustomLib.exe
echo ========================================
echo.
echo Press any key to exit.
pause >nul
