@echo off
rem Builds EhsLink.dll (EuroScope plugin, 32-bit) and ehs_test.exe.
rem Needs Visual Studio 2022 Build Tools (C++). EuroScope SDK path can be overridden with ES_SDK.
setlocal
if "%ES_SDK%"=="" set "ES_SDK=%APPDATA%\EuroScope\PlugIn"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if "%VSDIR%"=="" if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC" set "VSDIR=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools"
if "%VSDIR%"=="" (echo Visual Studio C++ build tools not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1

cd /d "%~dp0"
if not exist build mkdir build
set "CFLAGS=/nologo /std:c++17 /EHsc /O2 /MT /W3 /D_CRT_SECURE_NO_WARNINGS /utf-8"

cl %CFLAGS% /LD /I"%ES_SDK%" /Iinclude /Fobuild\ /Febuild\EhsLink.dll ^
   src\EhsLinkPlugin.cpp src\EhsCore.cpp src\EhsAlerts.cpp src\EhsBridge.cpp ^
   /link "%ES_SDK%\EuroScopePlugInDll.lib" ws2_32.lib winmm.lib user32.lib || exit /b 1

cl %CFLAGS% /Fobuild\ /Febuild\ehs_test.exe test\test_main.cpp src\EhsCore.cpp src\EhsAlerts.cpp ^
   /link ws2_32.lib user32.lib || exit /b 1

echo.
echo Built build\EhsLink.dll and build\ehs_test.exe
