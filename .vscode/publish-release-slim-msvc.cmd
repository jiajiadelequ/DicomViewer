@echo off
setlocal

set "PUBLISH_SUBDIR=release-slim"
set "WINDEPLOYQT_EXTRA_ARGS="

call "%~dp0publish-release-msvc.cmd"
exit /b %errorlevel%
