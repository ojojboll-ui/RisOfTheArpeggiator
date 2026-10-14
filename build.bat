@echo off
rem Builds Rise of the Arpeggiator and copies the exe next to this file.
rem
rem   build.bat               builds in .\build
rem   build.bat C:\somewhere  builds there instead (a short path helps if
rem                           MSBuild complains about path length)
rem
rem Needs CMake 3.22+, Git and Visual Studio 2022 (or its Build Tools) with
rem the C++ workload. Set CMAKE_GENERATOR to use another generator.

setlocal
set "ROOT=%~dp0"
set "BUILD=%~1"
if "%BUILD%"=="" set "BUILD=%ROOT%build"

if not defined CMAKE_GENERATOR set "CMAKE_GENERATOR=Visual Studio 17 2022"
set "ARCH="
echo %CMAKE_GENERATOR% | findstr /b /c:"Visual Studio" >nul && set "ARCH=-A x64"

cmake -S "%ROOT%." -B "%BUILD%" %ARCH% || exit /b 1
cmake --build "%BUILD%" --config Release --target Neonpuls || exit /b 1

rem Multi-config generators put it in Release\, single-config ones do not.
set "EXE=%BUILD%\Release\RiseOfTheArpeggiator.exe"
if not exist "%EXE%" set "EXE=%BUILD%\RiseOfTheArpeggiator.exe"
copy /Y "%EXE%" "%ROOT%RiseOfTheArpeggiator.exe" || exit /b 1
echo Built %ROOT%RiseOfTheArpeggiator.exe
