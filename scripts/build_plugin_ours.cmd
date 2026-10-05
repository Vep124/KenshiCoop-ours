@echo off
REM Build KenshiCoop.dll with a PORTABLE VC++ 2010 SP1 (v100) x64 toolchain:
REM   compiler + CRT : extracted from KB2519277 (vc_stdamd64.msi + vc_stdx86.msi)
REM   Windows headers: Windows SDK 7.1 (WinSDKBuild_amd64 component, admin-installed
REM                    into toolchain\sdk71root, no system changes)
REM   Boost libs     : third_party/KenshiLib_deps/boost_1_60_0/stage/lib (vc100)
REM No VS2010 / SDK install needed; nothing is written outside the repo.
REM
REM   Usage:  scripts\build_plugin_ours.cmd [Harness|Release|Debug]
setlocal

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Harness"

set "REPO=%~dp0.."
pushd "%REPO%" >nul
set "REPO=%CD%"
popd >nul

set "VCX64=D:\kenshi-mp\toolchain\vc10\Program Files(64)\Microsoft Visual Studio 10.0\VC"
set "VCX86=D:\kenshi-mp\toolchain\vc10x86\Program Files\Microsoft Visual Studio 10.0\VC"
set "SDK=D:\kenshi-mp\toolchain\sdk71root\Program Files\Microsoft SDKs\Windows\v7.1"
set "KL=%REPO%\third_party\KenshiLib_deps"
set "ENET=%REPO%\third_party\enet\enet\include"
set "VCTARGETS=D:\kenshi-mp\toolchain\vctargets"

set "MSBUILD="
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" 2^>nul`) do set "MSBUILD=%%i"
if not defined MSBUILD set "MSBUILD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"

set "PATH=%VCX64%\bin\amd64;%VCX64%\bin;%SDK%\Bin\x64;%SDK%\Bin;%PATH%"
set "INCLUDE=%VCX86%\include;%SDK%\Include;%REPO%\third_party\vc10_compat;%KL%\KenshiLib\Include;%KL%\KenshiLib\Include\ogre;%KL%\boost_1_60_0;%ENET%"
set "LIB=%VCX64%\lib\amd64;%SDK%\Lib\x64;%KL%\KenshiLib\Libraries;%KL%\boost_1_60_0\stage\lib"

echo === Building KenshiCoop.dll (%CONFIG%^|x64, portable v100) ===
where cl.exe

"%MSBUILD%" "%REPO%\src\plugin\KenshiCoop.vcxproj" /p:Configuration=%CONFIG% /p:Platform=x64 /p:UseEnv=true /p:TrackFileAccess=false /p:DisableRegistryUse=true /p:VCTargetsPath=%VCTARGETS%\ /p:VCToolsVersion=10.0.40219.01 /p:VCToolsInstallDir="%VCX64%" /p:EmbedManifest=false /p:LinkIncremental=false /nologo /v:minimal

endlocal

