@echo off
setlocal

call "F:\VS\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

set "QT_ROOT=D:\Qt5\5.15.2\msvc2019_64"
set "WINDEPLOYQT_EXE=%QT_ROOT%\bin\windeployqt.exe"
set "BUILD_SCRIPT=%~dp0build-release-msvc.cmd"
set "SRC_DIR=%~dp0.."
for %%I in ("%SRC_DIR%") do set "SRC_DIR=%%~fI"
set "BUILD_DIR=%SRC_DIR%\build\release"
set "PUBLISH_DIR=%SRC_DIR%\release"

if not exist "%WINDEPLOYQT_EXE%" (
    echo windeployqt not found: "%WINDEPLOYQT_EXE%"
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

for %%E in (exe dll pdb) do (
    if exist "%BUILD_DIR%\*.%%E" copy /Y "%BUILD_DIR%\*.%%E" "%PUBLISH_DIR%\" >nul
)

"%WINDEPLOYQT_EXE%" --release --compiler-runtime --no-translations "%PUBLISH_DIR%\DicomViewerSkeleton.exe"
if errorlevel 1 exit /b %errorlevel%

echo.
echo Publish complete: "%PUBLISH_DIR%"
exit /b 0
