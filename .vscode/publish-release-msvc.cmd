@echo off
setlocal

call "F:\VS\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

set "QT_ROOT=D:\Qt5\5.15.2\msvc2019_64"
set "WINDEPLOYQT_EXE=%QT_ROOT%\bin\windeployqt.exe"
set "CMAKE_EXE=F:\VS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "BUILD_SCRIPT=%~dp0build-release-msvc.cmd"
set "RUNTIME_SCRIPT=%~dp0copy-runtime-deps.cmake"
set "SRC_DIR=%~dp0.."
for %%I in ("%SRC_DIR%") do set "SRC_DIR=%%~fI"
set "BUILD_DIR=%SRC_DIR%\build\release"
if not defined PUBLISH_SUBDIR set "PUBLISH_SUBDIR=release"
if not defined WINDEPLOYQT_EXTRA_ARGS set "WINDEPLOYQT_EXTRA_ARGS="
set "PUBLISH_DIR=%SRC_DIR%\%PUBLISH_SUBDIR%"

if not exist "%WINDEPLOYQT_EXE%" (
    echo windeployqt not found: "%WINDEPLOYQT_EXE%"
    exit /b 1
)

if not exist "%CMAKE_EXE%" (
    echo cmake not found: "%CMAKE_EXE%"
    exit /b 1
)

if not exist "%RUNTIME_SCRIPT%" (
    echo runtime dependency script not found: "%RUNTIME_SCRIPT%"
    exit /b 1
)

call "%BUILD_SCRIPT%"
if errorlevel 1 exit /b %errorlevel%

if not exist "%BUILD_DIR%\DicomViewerSkeleton.exe" (
    echo Release executable not found: "%BUILD_DIR%\DicomViewerSkeleton.exe"
    exit /b 1
)

if exist "%PUBLISH_DIR%" rmdir /s /q "%PUBLISH_DIR%"
mkdir "%PUBLISH_DIR%"
if errorlevel 1 exit /b %errorlevel%

copy /Y "%BUILD_DIR%\DicomViewerSkeleton.exe" "%PUBLISH_DIR%\" >nul
if errorlevel 1 exit /b %errorlevel%

"%WINDEPLOYQT_EXE%" --release --compiler-runtime --no-translations %WINDEPLOYQT_EXTRA_ARGS% "%PUBLISH_DIR%\DicomViewerSkeleton.exe"
if errorlevel 1 exit /b %errorlevel%

"%CMAKE_EXE%" ^
 -DAPP_PATH="%PUBLISH_DIR%\DicomViewerSkeleton.exe" ^
 -DPUBLISH_DIR="%PUBLISH_DIR%" ^
 -DBUILD_DIR="%BUILD_DIR%" ^
 -P "%RUNTIME_SCRIPT%"
if errorlevel 1 exit /b %errorlevel%

echo.
echo Publish complete: "%PUBLISH_DIR%"
exit /b 0
