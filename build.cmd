@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "SOLUTION=%SCRIPT_DIR%ShadertoyShaderSelectorAndVideoRender_V0.sln"
set "CONFIGURATION=%~1"
set "PLATFORM=%~2"
set "TARGET=%~3"

if "%CONFIGURATION%"=="" set "CONFIGURATION=Debug"
if "%PLATFORM%"=="" set "PLATFORM=x64"
if "%TARGET%"=="" set "TARGET=Build"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "MSBUILD_EXE="

if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\amd64\MSBuild.exe`) do (
        if not defined MSBUILD_EXE set "MSBUILD_EXE=%%I"
    )
)

if not defined MSBUILD_EXE (
    for %%I in (
        "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe"
    ) do (
        if exist %%~I (
            if not defined MSBUILD_EXE set "MSBUILD_EXE=%%~I"
        )
    )
)

if not defined MSBUILD_EXE (
    echo Failed to locate MSBuild.exe.
    exit /b 1
)

echo Using MSBuild: %MSBUILD_EXE%
echo Building %SOLUTION% [%CONFIGURATION%^|%PLATFORM%] target=%TARGET%

"%MSBUILD_EXE%" "%SOLUTION%" /t:%TARGET% /p:Configuration=%CONFIGURATION% /p:Platform=%PLATFORM%
exit /b %ERRORLEVEL%
