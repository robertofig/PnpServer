@echo off

mkdir ..\prime
call cl prime.c /I ..\..\src /LD /Oi /O2 /link /DLL /EXPORT:ModuleMain /EXPORT:AppInit
move prime.dll ..\prime\prime.dll
copy primes ..\prime\primes

mkdir ..\modify
call cl modify.cpp /I ..\..\src /LD /Oi /O2 /link /DLL /EXPORT:ModuleMain
move modify.dll ..\modify\modify.dll

del *.lib *.exp *.obj *.ilk