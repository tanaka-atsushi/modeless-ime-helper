@echo off
rem Builds the exe (build.bat, release) and then the installer with Inno Setup 7 or 6.
rem Usage: installer.bat [version]     -> build\installer\ModelessImeHelper-Setup-<version>.exe
setlocal
cd /d "%~dp0"
call "%~dp0build.bat" || exit /b 1

rem Inno Setup installs per-user (%LOCALAPPDATA%\Programs) or per-machine; prefer 7, then 6, then PATH.
rem (No parenthesized blocks below: expanded paths such as "Program Files (x86)" would break them.)
set "ISCC="
for %%d in ("%LOCALAPPDATA%\Programs\Inno Setup 7" "%ProgramFiles%\Inno Setup 7" "%ProgramFiles(x86)%\Inno Setup 7" "%LOCALAPPDATA%\Programs\Inno Setup 6" "%ProgramFiles%\Inno Setup 6" "%ProgramFiles(x86)%\Inno Setup 6") do if not defined ISCC if exist "%%~d\ISCC.exe" set "ISCC=%%~d\ISCC.exe"
if not defined ISCC for %%i in (ISCC.exe) do set "ISCC=%%~$PATH:i"
if not defined ISCC echo Inno Setup (ISCC.exe) was not found. Install it: winget install JRSoftware.InnoSetup& exit /b 1

set "VERARG="
if not "%~1"=="" set "VERARG=/DAppVersion=%~1"
"%ISCC%" /Q %VERARG% installer\ModelessImeHelper.iss
exit /b %ERRORLEVEL%
