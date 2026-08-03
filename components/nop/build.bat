@echo off
REM Convenience build script for Windows hosts (Visual Studio or MinGW).
REM   build.bat            : configure + build + run tests in .\build
setlocal
set BUILD_DIR=build
if not "%~1"=="" set BUILD_DIR=%~1

echo ==^> configuring in %BUILD_DIR%
cmake -S . -B %BUILD_DIR% -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNOP_OSAL_PORT=windows
if errorlevel 1 exit /b 1

echo ==^> building
cmake --build %BUILD_DIR% --config RelWithDebInfo
if errorlevel 1 exit /b 1

echo ==^> running tests
cd %BUILD_DIR%
ctest -C RelWithDebInfo --output-on-failure
cd ..
echo ==^> done: artifacts in %BUILD_DIR%
endlocal
