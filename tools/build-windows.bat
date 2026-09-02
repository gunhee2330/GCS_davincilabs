@echo off
rem Build and deploy the police drone GCS on Windows.
rem
rem Three steps, and skipping any one of them fails in a way that looks like success:
rem
rem   1. vcvars64  - Ninja calls cl.exe directly, and a shell that has not run vcvars has no
rem                  INCLUDE path. The build then fails on <functional> and <stdint.h>, which
rem                  reads like a broken toolchain rather than a missing environment.
rem   2. build     - compiles into %BUILDDIR%\Debug
rem   3. install   - copies into %BUILDDIR%\staging, which is CMAKE_INSTALL_PREFIX and what the
rem                  Desktop shortcut actually runs. Build alone leaves the shortcut on the
rem                  previous binary, so changes appear to have had no effect.
rem
rem Usage:  tools\build-windows.bat [build-dir-name]
rem Default build dir is Windows-claude. Use your own name so two people never share one.

setlocal

set VSROOT=C:\Program Files\Microsoft Visual Studio\2022\Community
set CMAKE="%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

set BUILDNAME=%~1
if "%BUILDNAME%"=="" set BUILDNAME=Windows-claude

set REPO=%~dp0..
set BUILDDIR=%REPO%\build\%BUILDNAME%

if not exist %CMAKE% (
  echo [ERROR] cmake not found: %CMAKE%
  echo         Install Visual Studio 2022 with the "Desktop development with C++" workload.
  exit /b 1
)

echo [1/3] Visual Studio environment
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo [ERROR] vcvars64.bat failed
  exit /b 1
)

if not exist "%BUILDDIR%\CMakeCache.txt" (
  echo [1b/3] Configuring ^(first run, several minutes^)
  %CMAKE% -S "%REPO%" -B "%BUILDDIR%" -G Ninja ^
      -DCMAKE_BUILD_TYPE=Debug ^
      -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64
  if errorlevel 1 (
    echo [ERROR] configure failed
    exit /b 1
  )
)

echo [2/3] Building
%CMAKE% --build "%BUILDDIR%"
if errorlevel 1 (
  echo [ERROR] build failed
  exit /b 1
)

echo [3/3] Installing to staging
%CMAKE% --install "%BUILDDIR%"
if errorlevel 1 (
  echo [ERROR] install failed
  exit /b 1
)

echo.
echo Done: %BUILDDIR%\staging\bin\QGroundControl.exe
endlocal
