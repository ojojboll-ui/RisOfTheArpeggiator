@rem Builds Rise of the Arpeggiator (working name Neonpuls) and copies the exe next to this file.
@rem The build tree lives at a short path: MSBuild's FileTracker fails under
@rem deep ones. SDL3 is reused from Chordtroller's checkout when it is there.
cmake -S "%~dp0." -B C:\Users\oloff\npbuild -G "Visual Studio 17 2022" -A x64 || exit /b 1
cmake --build C:\Users\oloff\npbuild --config Release --target Neonpuls || exit /b 1
copy /Y C:\Users\oloff\npbuild\Release\RiseOfTheArpeggiator.exe "%~dp0RiseOfTheArpeggiator.exe"
