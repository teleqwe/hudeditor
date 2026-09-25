# schemereload

A Counter-Strike: Source (64-bit) plugin that applies changes to `SourceScheme.res`,
`ClientScheme.res` and `ChatScheme.res` while the game is running, so colour tweaks no longer need a restart.

## Install
`build.bat` builds the plugin and copies it into the game:

```
cstrike/addons/schemereload.dll
cstrike/addons/schemereload.vdf   <- tells the game to load the dll at startup
```

## Use
1. Add `-insecure` to the game's launch options (Steam > Counter-Strike: Source >
   Properties > Launch Options). Plugins don't load without it, and it keeps VAC out
   of the picture. **Remove it before playing on normal servers.**
2. Start the game and open the console. You should see:
   `[schemereload] loaded.` and a moment later `found SourceScheme in memory` /
   `found ClientScheme in memory`.
3. Edit a scheme file in `cstrike/custom/<your hud>/resource/` and save.
   Within about a second the console prints `[schemereload] reloaded: ...` and the
   colours change. Reopen a window if it hasn't updated.

## Editor
`app/CSSHudEditor.exe` carries this plugin and installs it (see `app/README.md`).

The plugin also watches `scripts/HudLayout.res` and `scripts/hudanimations.txt` and runs
`hud_reloadscheme` when they change (in a map).

## Console commands
| Command | What it does |
|---|---|
| `scheme_reload` | Reload right now |
| `scheme_reload_status` | Show which files are used, how they matched the schemes in memory, and any switched-off steps |
| `scheme_reload_watch 0/1` | Turn automatic reload-on-save off/on (default 1) |
| `scheme_reload_fonts 0/1` | Apply font changes too (experimental, default 1) |
| `scheme_font_info <font>` | Which Windows font and size the game really uses for a scheme font |
| `schemereload_testtext <n>` | Sample server texts to keep on screen while in a map, added up: 1 timer hint box and right-side key hint, 2 speed (centre print), 4 jhud, 8 top-left record times, 16 a chat line every 3 s (SayText). Ones switched off are cleared |
| `schemereload_mount <folder>` | Search this folder before all others for game files: the HUD being edited wins over other custom folders, and a folder made after the game started is found. A folder that wasn't a search path before is taken out again when another one is mounted |
| `plugin_print` | Engine command: lists loaded plugins |

For the editor, the plugin also:
- writes the visible panel tree to `addons/schemereload_panels.txt`: depth, module, name, box, class and the scheme file it draws with, rewritten
  only when it changes;
- runs any console commands written to `addons/schemereload_cmd.txt` as if typed into the console (so client commands
  such as `+showscores` work too), then deletes the file;
- registers new `.ttf` files listed in `CustomFontFiles` and resets the glyph cache, so font face changes show live;
- watches `resource/UI/*.res` next to ClientScheme;
- on load, searches `addons/schemereload_sounds` first if it exists: the editor puts a silent `sound/ui/hint.wav`
  there, as the test texts' hints would otherwise beep ten times a second.

## What updates live
- Colours in `Colors` and `BaseSettings`, and other `BaseSettings` values (`MainMenu.MenuItemHeight`...)
- Border colours and definitions, including border blocks added after the game started and names pointed at a
  different border (`ButtonBorder "DepressedBorder"`). vgui only makes border objects at startup, so the plugin draws
  those itself and puts them in the scheme's border list. If that list can't be found it says so once, and they show
  after a restart.
- Lines and settings taken out of a border or font block go back to unset
- The main menu (MainMenu.* colours). Refreshing it would leave the logo see-through and the items small (the menu only
  sets those at startup), so the plugin keeps the logo's alpha and the items' fonts across a reload, and scales the
  item height to the screen as the menu does at startup.
- Font sizes/settings for fonts that already exist

Tip: CS:S HUD numbers (health, armor, money, ammo) use `Panel.FgColor`, not `FgColor`.

Still needs a restart: new font names that didn't exist when the game started.

## Troubleshooting
- **No `[schemereload]` lines at startup**: check `-insecure` is set and run `plugin_print`.
  You can also load it by hand from the main menu: `plugin_load addons/schemereload`.
- **"X not found in memory"**: run `scheme_reload_status`. The scheme marked `<- in use`
  should have a clearly positive match score; paste the output if not.
- **"the 'X' step crashed and is now switched off"**: each step (colours, fonts, borders,
  refresh panels, hud_reloadscheme) is protected separately. The named one stops, the rest
  keep working. Restart the game to re-enable it.
- **Game crashes on reload**: delete `cstrike/addons/schemereload.vdf` to stop it loading.

## Rebuilding
Needs Visual Studio 2022 Build Tools (C++) and the Source SDK 2013 sparse checkout in
`sdk/` next to `build.bat` (or set `SDK` to another checkout's `src` folder). Quit the game,
then run `build.bat`.
