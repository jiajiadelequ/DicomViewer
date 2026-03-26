@echo off
setlocal

call "F:\VS\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

set "QT_ROOT=D:\Qt5\5.15.2\msvc2019_64"
set "CMAKE_EXE=F:\VS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA_EXE=F:\VS\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "BUILD_DIR=%~dp0..\build\release"
set "SRC_DIR=%~dp0.."

"%CMAKE_EXE%" -S "%SRC_DIR%" -B "%BUILD_DIR%" ^
 -G Ninja ^
 -D CMAKE_BUILD_TYPE=Release ^
 -D CMAKE_PREFIX_PATH="%QT_ROOT%" ^
 -D Qt5_DIR="%QT_ROOT%\lib\cmake\Qt5" ^
 -D CMAKE_MAKE_PROGRAM="%NINJA_EXE%"
if errorlevel 1 exit /b %errorlevel%

"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release
exit /b %errorlevel%
