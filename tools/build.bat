@rem Builds and runs one offline test, e.g.  build.bat audiotest [outdir]
@rem The audio headers are plain C++, so the tests need no SDL, no window and no pad.
@call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++17 /O2 /EHsc /W3 /utf-8 %1.cpp /Fe:%1.exe /Fo:%1.obj && .\%1.exe %2
