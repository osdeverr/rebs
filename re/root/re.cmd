@echo off

if "%1"=="build" goto run_tool
if "%1"=="env" goto run_tool
if "%1"=="install" goto run_tool
if "%1"=="new" goto run_tool
if "%1"=="help" goto run_tool
if "%1"=="run" goto run_tool

if "%1"=="b" goto default_build
if "%1"=="cfg" goto default_build
if "%1"=="config" goto default_build
if "%1"=="c" goto default_build
if "%1"=="do" goto default_build
if "%1"=="meta" goto default_build
goto default_build

echo Invalid Re command: %1
echo Try 're help'.
goto end

:run_tool
set TOOL=%0\..\re-tool-%1
%TOOL% %*
goto end

:default_build
set TOOL=%0\..\re-tool-build
%TOOL% %*
goto end

:end
