@echo off

SET Includes= /I ..\include
SET Arch= /arch:AVX2
SET ReleaseCL= /Oi /O2 /EHa- /GS-
SET ReleaseLink= /link /ENTRY:Entry /FIXED /INCREMENTAL:NO /OPT:ICF /OPT:REF /SUBSYSTEM:CONSOLE libvcruntime.lib ucrt.lib
SET DebugCL= /Zi /MTd /RTC1 /DPNP_DEBUG
SET DebugLink= /link /INCREMENTAL:NO /SUBSYSTEM:CONSOLE

if not exist ..\build mkdir ..\build
pushd ..\build
call cl ..\src\pnp-server-entry-win32.cpp /Fe:pnp-server.exe %Includes% %Arch% %ReleaseCL% %ReleaseLink%
call cl ..\src\pnp-server-entry-win32.cpp /Fe:pnp-server-debug.exe %Includes% %Arch% %DebugCL% %DebugLink%
del *.obj
popd