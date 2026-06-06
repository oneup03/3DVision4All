@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 ( echo vcvars32 failed & exit /b 1 )
cd /d "%~dp0"

set TOOLSET_OVERRIDE=/p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0

rem --- Build third-party dependencies first (each is in its own submodule sln) ---

echo === Building Obj2Inc (Deviare2) ===
msbuild third_party\Deviare2\Externals\DeviareInProc\Obj2Inc\Obj2Inc_2017.vcxproj ^
    /p:Configuration=Release /p:Platform=Win32 %TOOLSET_OVERRIDE% /m /v:minimal
if errorlevel 1 ( echo Obj2Inc build failed & exit /b 1 )

echo === Building NktHookLib (Deviare2) ===
msbuild third_party\Deviare2\Externals\DeviareInProc\NktHookLib\NktHookLib_2017.vcxproj ^
    /p:Configuration=Release /p:Platform=Win32 %TOOLSET_OVERRIDE% /m /v:minimal
if errorlevel 1 ( echo NktHookLib build failed & exit /b 1 )

echo === Building SimulatedReality (SR-lib) ===
msbuild third_party\SR-lib\SimulatedReality.vcxproj ^
    /p:Configuration=Release-MD /p:Platform=Win32 /m /v:minimal
if errorlevel 1 ( echo SR-lib build failed & exit /b 1 )

echo === Building 3DVision4All ===
msbuild 3DVision4All.sln /p:Configuration=Release /p:Platform=Win32 /m /v:minimal
if errorlevel 1 ( echo 3DVision4All build failed & exit /b 1 )

echo === Build complete: see bin\Win32-Release\ ===
exit /b 0
