@echo off
rem Builds schemereload.dll (64-bit) and copies it into Counter-Strike: Source's addons folder.
rem Needs: Visual Studio 2022 Build Tools (C++), Source SDK 2013 checked out at %SDK%.
setlocal

if "%SDK%"=="" set SDK=%~dp0sdk\src
set GAME=C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Source\cstrike
set OUT=%~dp0build

if not exist "%SDK%\public\tier1\KeyValues.h" (
	echo Source SDK not found at %SDK%
	exit /b 1
)

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%OUT%" mkdir "%OUT%"

set DEFS=/D VPC /D WIN32 /D _WIN32 /D WIN64 /D _WIN64 /D PLATFORM_64BITS /D COMPILER_MSVC /D COMPILER_MSVC64 ^
 /D NDEBUG /D _WINDOWS /D _USRDLL /D _MBCS /D _CRT_SECURE_NO_DEPRECATE /D _CRT_NONSTDC_NO_DEPRECATE ^
 /D _ALLOW_RUNTIME_LIBRARY_MISMATCH /D _ALLOW_ITERATOR_DEBUG_LEVEL_MISMATCH /D _ALLOW_MSC_VER_MISMATCH ^
 /D _DLL_EXT=.dll /D DLLNAME=schemereload
set INC=/I "%SDK%\public" /I "%SDK%\public\tier0" /I "%SDK%\public\tier1" /I "%SDK%\common"
set CFLAGS=/nologo /c /O2 /MT /fp:fast /GS- /W3 /Gw /Zc:inline /Zc:threadSafeInit- /Zc:__cplusplus /MP ^
 /wd4316 /wd5033 /wd5054 /wd5055 /wd4577 /wd4091 /wd4355 /wd4101 /wd4005 /wd4244 /wd4267 /wd4996

set T1=%SDK%\tier1
cl %CFLAGS% %DEFS% %INC% /Fo"%OUT%\\" ^
 "%~dp0src\schemereload.cpp" "%~dp0src\win32_helpers.cpp" "%~dp0src\vgui_slots.cpp" "%~dp0src\cvar_bounds.cpp" "%SDK%\public\tier0\memoverride.cpp" ^
 "%T1%\KeyValues.cpp" "%T1%\convar.cpp" "%T1%\interface.cpp" "%T1%\tier1.cpp" "%T1%\strtools.cpp" ^
 "%T1%\strtools_unicode.cpp" "%T1%\utlbuffer.cpp" "%T1%\utlsymbol.cpp" "%T1%\utlstring.cpp" ^
 "%T1%\characterset.cpp" "%T1%\generichash.cpp" "%T1%\mempool.cpp" "%T1%\memstack.cpp" ^
 "%T1%\stringpool.cpp" "%T1%\splitstring.cpp" "%T1%\exprevaluator.cpp" "%T1%\commandbuffer.cpp" ^
 "%T1%\utlbinaryblock.cpp" "%T1%\kvpacker.cpp" "%T1%\keyvaluesjson.cpp" "%T1%\bitbuf.cpp" ^
 "%T1%\checksum_crc.cpp" "%T1%\checksum_md5.cpp" "%T1%\byteswap.cpp" "%T1%\processor_detect.cpp" ^
 || exit /b 1

link /nologo /DLL /OUT:"%OUT%\schemereload.dll" "%OUT%\*.obj" ^
 "%SDK%\lib\public\x64\tier0.lib" "%SDK%\lib\public\x64\vstdlib.lib" ^
 user32.lib advapi32.lib shell32.lib ws2_32.lib || exit /b 1

if not exist "%GAME%\addons" mkdir "%GAME%\addons"
copy /y "%OUT%\schemereload.dll" "%GAME%\addons\schemereload.dll" >nul || (
	echo Couldn't copy into addons - is the game running? Quit it and build again.
	exit /b 1
)
copy /y "%~dp0schemereload.vdf" "%GAME%\addons\schemereload.vdf" >nul
echo Installed to %GAME%\addons
