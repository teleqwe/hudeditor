@echo off
rem Closes the game and the editor (their files are locked while they run), then builds the plugin and the exe.
setlocal
set GAME=C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Source\cstrike

tasklist /fi "imagename eq cstrike_win64.exe" | find /i "cstrike_win64.exe" >nul && (
	rem ask the game to quit through the plugin's command file, then make sure
	echo quit> "%GAME%\addons\schemereload_cmd.txt"
	ping -n 5 127.0.0.1 >nul
	taskkill /im cstrike_win64.exe /f >nul 2>&1
)
taskkill /im CSSHudEditor.exe >nul 2>&1
ping -n 3 127.0.0.1 >nul
taskkill /im CSSHudEditor.exe /f >nul 2>&1

call "%~dp0hudreload\build.bat" || exit /b 1
call "%~dp0app\build.bat" || exit /b 1
