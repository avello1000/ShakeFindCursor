@echo off
rem ShakeFindCursor build script.
rem Uses the locally installed MSVC (VS2022 Build Tools). Run from this directory.

setlocal
set "SRC=%~dp0"
cd /d "%SRC%"

rem Locate vcvars64.bat: ask vswhere first (covers Community/Professional/
rem Enterprise/BuildTools), then fall back to well-known paths.
set "VCVARS="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
    if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found. Install the "Desktop development with C++"
    echo         workload, or edit "VCVARS" in build.bat to point at your MSVC install.
    exit /b 1
)

rem vcvars may have changed the working directory; come back to the script dir.
cd /d "%SRC%"

rem Remove the previous binary first, so a stale exe can never masquerade as success.
if exist "%SRC%ShakeFindCursor.exe" del /q "%SRC%ShakeFindCursor.exe"

rem Resources: version info + app icon (app.res is linked into the exe below).
rem /c 65001: app.rc is UTF-8 (the FileDescription contains Chinese text).
rc /nologo /c 65001 "%SRC%app.rc"
if errorlevel 1 (
    echo [FAIL] Resource compilation failed.
    exit /b 1
)

echo Building ShakeFindCursor...
rem 实测 SetSystemCursor 不需要管理员权限，故使用 asInvoker —— 启动不再弹 UAC。
rem 若在个别机器上发现放大失效（光标方案由组策略统管等），可把下面这行改成
rem   /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'"
cl /nologo /EHsc /O2 /utf-8 /DUNICODE /D_UNICODE "%SRC%main.cpp" "%SRC%app.res" ^
   /link user32.lib gdi32.lib shell32.lib advapi32.lib gdiplus.lib /SUBSYSTEM:WINDOWS ^
   /MANIFESTUAC:"level='asInvoker' uiAccess='false'" ^
   /OUT:"%SRC%ShakeFindCursor.exe"

echo.
if errorlevel 1 (
    echo [FAIL] Compilation failed.
    exit /b 1
)
if not exist "%SRC%ShakeFindCursor.exe" (
    echo [FAIL] Build produced no exe.
    exit /b 1
)

echo [OK] Build succeeded: %SRC%ShakeFindCursor.exe
endlocal
