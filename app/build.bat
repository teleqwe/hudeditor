@echo off
rem Builds CSSHudEditor.exe: the editor in a WebView2 window, with the schemereload plugin packed inside.
rem Needs: Visual Studio 2022 Build Tools (C++), hudreload\build\schemereload.dll (run hudreload\build.bat first),
rem and the WebView2 SDK in webview2\ (the Microsoft.Web.WebView2 NuGet package, unzipped).
setlocal
set OUT=%~dp0build
if not exist "%~dp0..\hudreload\build\schemereload.dll" (
	echo hudreload\build\schemereload.dll missing - run hudreload\build.bat first
	exit /b 1
)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%OUT%" mkdir "%OUT%"
cd /d "%~dp0src"

rc /nologo /fo "%OUT%\app.res" app.rc || exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++17 /W3 /D UNICODE /D _UNICODE /I "%~dp0webview2\build\native\include" ^
 /Fo"%OUT%\\" main.cpp /link /SUBSYSTEM:WINDOWS /OUT:"%~dp0CSSHudEditor.exe" "%OUT%\app.res" ^
 "%~dp0webview2\build\native\x64\WebView2LoaderStatic.lib" ^
 user32.lib gdi32.lib ole32.lib oleaut32.lib shell32.lib shlwapi.lib advapi32.lib version.lib || exit /b 1
echo Built %~dp0CSSHudEditor.exe
