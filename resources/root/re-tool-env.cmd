@echo off

set RE_PLATFORM=windows
set RE_ARCH=%3

call %0\..\data\env-scripts\%2.cmd %*
