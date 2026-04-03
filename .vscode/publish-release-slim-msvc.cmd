@echo off
setlocal

set "PUBLISH_SUBDIR=release-slim"
set "WINDEPLOYQT_EXTRA_ARGS=--no-opengl-sw --no-system-d3d-compiler"

call "%~dp0publish-release-msvc.cmd"
exit /b %errorlevel%
