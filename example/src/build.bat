@echo off

pushd ..
call cl src\prime.c /I ..\src /LD /Oi /O2 /link /DLL /EXPORT:ModuleMain /EXPORT:AppInit
call cl src\modify.cpp /I ..\src /LD /Oi /O2 /link /DLL /EXPORT:ModuleMain
copy src\primes .\primes
del *.lib *.exp *.obj *.ilk
popd