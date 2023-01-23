@echo off

SET TinyBase= ..\include\TinyBase\src
SET TinyServer= ..\include\TinyServer\src
:: SET Arch= /arch:(AVX | AVX2 | AVX512)
SET ReleaseCL= /Oi /O2 /EHa- /GS-
SET ReleaseLink= /link /ENTRY:Entry /FIXED /INCREMENTAL:NO /OPT:ICF /OPT:REF /SUBSYSTEM:CONSOLE libvcruntime.lib ucrt.lib
SET DebugCL= /Zi /MTd /RTC1 /DPNP_DEBUG
SET DebugLink= /link /INCREMENTAL:NO /SUBSYSTEM:CONSOLE

if not exist ..\build mkdir ..\build
pushd ..\build
call cl ..\src\pnp-server-entry-win32.cpp /Fe:pnp-server.exe /I %TinyBase% /I %TinyServer% %Arch% %ReleaseCL% %ReleaseLink%
call cl ..\src\pnp-server-entry-win32.cpp /Fe:pnp-server-debug.exe /I %TinyBase% /I %TinyServer% %Arch% %DebugCL% %DebugLink%
del *.obj
popd