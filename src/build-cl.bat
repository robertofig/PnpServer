@echo off

SET ReleaseCL= /Oi /O2 /EHa- /GS-
SET ReleaseLink= /link /FIXED /INCREMENTAL:NO /OPT:ICF /OPT:REF /SUBSYSTEM:CONSOLE
SET DebugCL= /Zi /MTd /RTC1 /DPNP_DEBUG /arch:AVX2
SET DebugLink= /link /INCREMENTAL:NO /SUBSYSTEM:CONSOLE

if not exist ..\build mkdir ..\build
pushd ..\build
call cl ..\src\pnp-server.cpp /Fe:pnp-server.exe /I ..\include %ReleaseCL% %ReleaseLink%
call cl ..\src\pnp-server.cpp /Fe:pnp-server-debug.exe /I ..\include %DebugCL% %DebugLink%
del *.obj
popd