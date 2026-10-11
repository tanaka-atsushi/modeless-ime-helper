@echo off
rem Builds build\ime2nd.exe with CMake + Ninja + MSVC. Run from any prompt.
rem Usage: build.bat [debug]     (default: release; debug uses build-debug\)
setlocal
set "PRESET=release"
if /i "%~1"=="debug" set "PRESET=debug"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
rem (No parenthesized blocks below: expanded paths such as "Program Files (x86)" would break them.)
if not exist "%VSWHERE%" echo vswhere.exe not found& exit /b 1
set "VSROOT="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT echo Visual Studio with the C++ x64 toolset was not found.& exit /b 1

rem vcvars64.bat shells out to vswhere via PATH and prints an error if it is missing.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

rem Prefer cmake/ninja on PATH, otherwise use the copies bundled with Visual Studio.
where cmake >nul 2>nul || set "PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
where ninja >nul 2>nul || set "PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

cd /d "%~dp0"
cmake --preset %PRESET% || exit /b 1
cmake --build --preset %PRESET%
exit /b %ERRORLEVEL%
