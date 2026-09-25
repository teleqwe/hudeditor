# CS:S HUD Editor

A live HUD editor for Counter-Strike: Source (64-bit). Click anything in the game and change its colours, fonts,
boxes, borders and position. The game updates as you edit, with no restart.

It's a single `CSSHudEditor.exe`. It starts the game windowed with a small plugin, shows the game inside the editor,
and writes your changes into a normal HUD folder in `cstrike/custom`. The plugin makes the running game pick those
changes up straight away.

## What you can edit
- **HUD parts** (timer, chat, weapon selection, kill feed, health...): show or hide, colours, box shape and corners,
  outlines, position, size and fonts.
- **Windows and menus** (main menu, options, server browser, team select, buy menu, MOTD, console, spectator bars):
  click the exact button, list or text and edit its colours, borders, font and position.
- **Scoreboard:** background, text colours, your own row's highlight and all its fonts. A button keeps it open
  while you edit.
- **Fonts:** face, size, weight and effects for every font, or one face and weight for all text at once.
- **Test server:** one click starts an offline map with bhop/surf-style timer, speed and chat text on screen, so
  those parts can be styled.

Changes are kept only when you click **Done** or **Save HUD**; anything unsaved is put back when you close.

## Using it
See [app/README.md](app/README.md). Short version: quit the game, run `CSSHudEditor.exe`, and pick or create a HUD.

The plugin needs the game started with `-insecure`. The editor adds that to its own launch only and never to your
Steam launch options, so playing normally from Steam stays VAC-secured and plugin-free.

## Layout
| Folder | What's in it |
|---|---|
| `app/` | The Windows host (`src/main.cpp`): WebView2 window, launching the game, file access, unsaved-change backups |
| `editor/` | The editor itself, one `index.html` (packed into the exe) |
| `hudreload/` | `schemereload`, the game plugin that applies scheme, font, border and layout changes live and reports what's on screen |
| `app/defaults/` | The game's default scheme and HUD files the editor starts new HUDs from (packed into the exe) |
| `vanilla/` | The game's own HUD files with notes, kept as a reference |

## Building
Needs Windows, Visual Studio 2022 Build Tools (C++) and two SDKs that aren't in this repo:
1. **Source SDK 2013** in `hudreload/sdk`, as a sparse checkout:
   ```
   git clone --filter=blob:none --sparse https://github.com/ValveSoftware/source-sdk-2013.git hudreload/sdk
   git -C hudreload/sdk sparse-checkout set src/common src/lib/public/x64 src/public src/tier1 src/vgui2/vgui_controls src/utils/serverplugin_sample src/vpc_scripts
   ```
2. **WebView2 SDK** in `app/webview2`: the `Microsoft.Web.WebView2` NuGet package (1.0.4191.47), unzipped there.

Then run `build-all.bat`. It closes the game and the editor, builds the plugin (and copies it into the game's
`addons` folder), then builds `app/CSSHudEditor.exe`. Checks: `CSSHudEditor.exe --selftest <empty folder>` and
`editor/index.html#test` in a browser.
