# CS:S HUD Editor

One file, `CSSHudEditor.exe`, with nothing to install: the live-reload plugin, the editor and the default files are
packed inside it. Needs Windows 10 or 11 (it uses the WebView2 runtime that ships with Windows 11).

## Use
1. Quit Counter-Strike: Source and open `CSSHudEditor.exe`. It starts the game windowed, with the plugin, and shows
   the game window in the editor on its own.
   - The game runs borderless at your resolution, so keep the editor on another monitor or make the game window
     smaller.
   - `-insecure` is used for that launch only and is never saved in your Steam launch options, so playing normally
     from Steam is unaffected (no plugins, VAC as usual).
2. Pick the HUD to edit. The list shows recent HUDs and HUD folders in `cstrike/custom`.
   - **Browse** opens a HUD from anywhere on disk.
   - **Start a new HUD** asks for a name and creates the HUD in your custom folder, starting from the default look.

   The HUD is edited where it is. While the editor runs, the plugin makes the game look in that HUD before any other
   folder in custom (normally they are searched alphabetically), so other HUDs there can't cover your changes.
3. **Test server** starts an offline game on de_dust2, with you on CT, a frozen bot on T and $16000. It keeps the
   bhop/surf-style texts on screen. The buttons under it switch each one on and off (remembered):
   - **Timer:** the timer hint box (HudHintText) and the right-side text (HudHintTextSmall). The hint's beep is
     silent while the editor runs: the plugin has the game use a silent `sound/ui/hint.wav` from
     `addons/schemereload_sounds`. Nothing goes into your HUD.
   - **1 Speed:** PrintCenterText, drawn with font Trebuchet24 from SourceScheme
   - **2 jhud:** ShowHudText/game_text just under it, drawn with font CenterPrintText from ClientScheme
   - **3 Top left:** record times in the top left corner, also ShowHudText with CenterPrintText
   - **Chat:** a chat line every 3 seconds (a player in team colour and a green `[Timer]` line), font ChatFont from
     ChatScheme

   Rounds never end on the test server (`mp_ignore_round_win_conditions 1`), so windows don't close mid-edit.

   **Scoreboard** keeps the scoreboard open until you click it again, and opens it again when a change reloads the
   HUD. Keys you press with the mouse over the preview (Esc, ~, B...) go to the game. Your chat key opens the chat,
   and it stays open for editing. The game comes to the front while the key is down and the editor takes the front back
   once you let go (a map otherwise locks the mouse inside the game).
   **Drag anything in the preview to move it.** HUD parts move in HudLayout, window controls in the window's `.res`
   (`xpos`/`ypos`, keeping `r` and `c` anchors). The dashed box shows where it goes, and the game follows once you
   pause. What the game places itself (main menu, server browser, window frames) can't be moved, and the radio menu
   only moves up and down (the game keeps it full width). The same X and Y positions are rows on every part that has
   them. The team, class and buy menus and the MOTD read positions at startup, so moves there show after a restart.
4. **Click anything in the preview** to edit it:
   - **HUD parts:** show/hide and opacity, colours, position and size, the font it uses, and its box:
     - **Corners:** rounded or square. Where the game can't draw one of them (the timer's box is always rounded) the
       option is greyed out. Boxes the game's code draws (chat, weapon selection) go square with square corner images
       the editor makes in your HUD (`materials/vgui/hudeditor`).
     - A part with a `border` in HudLayout takes its box shape from the border, and a running game keeps it: picking
       corners there takes the border out and asks you to restart the game.
     - HUDs made with an earlier version may have "square corners everywhere" (a replaced `vgui/hud/8x800corner1-4`
       material); the Corners row offers to remove it.
     - Weapon selection's and the radio menu's own colours only take the scheme colour at startup, so a change also
       names it in HudLayout (`NumberColor "SelectionNumberFg"`, `MenuItemColor "ItemColor"`...), which the game looks
       up again on a reload.
     - **Radio and server menus** (radio commands, SourceMod menus such as bhop checkpoints): title, option and
       background colours, the title and option fonts, and the position.
   - **Windows** (team select, buy menu, scoreboard, spectator bars, MOTD, main menu, options, console): the exact
     control you clicked. You get, in this order (colours, then fonts, then borders, then the rest):
     - its own colour and position (**Just this ...**)
     - the colours shared by every control of that type, and the window colours
     - the fonts it uses
     - its borders (normal, focused, pressed...): which sides draw (four tick boxes), thickness and colour. Sides that
       are off are written as clear lines, since a running game keeps lines a file no longer lists
     - for menu windows, square or rounded corners and the window edge
     - **Main menu:** hide or colour the logo (`Main.Title1.Color`, `Main.Title2.Color` in ClientScheme), the item
       height (`MainMenu.MenuItemHeight`, scaled to your screen) and the menu text colours. The item text size is the
       MenuLarge font. **Import a picture** (also found by searching "background") sets the menu background: it is
       cropped to fill the screen and written as `materials/console/background01` (4:3) and `background01_widescreen`
       (uncompressed VTFs). The game loads it at startup, so restart it to see a new one.
       **Buttons** (`resource/GameMenu.res`): remove, reorder, put back any of the game's own buttons, or add one that
       joins a server (type the IP:port). Also read at startup only.
     - **Server browser:** show or hide the Filters button (`servers/InternetGamesPage.res`, `CustomGamesPage.res`;
       read at startup).
     - **Chat:** click its parts in the preview to move or hide them (`resource/UI/BaseChat.res`, e.g. the Filters
       button), and set the padding inside it. Hidden parts are listed under "Chat: hidden parts" to bring back.
     - **Scroll bar width** (`ScrollBar.Wide`) shows with scroll bar colours, or search "scroll", for in-game windows,
       menus and the chat.
     - **Show on screen** to hide the control. It moves off screen and its position is kept in `xpos_hudeditor`. The
       game sets window parts' visibility itself, so `visible` can't hide them.
     - **Spectator bars:** show or hide the top and bottom bar, keeping their text. The game sizes these two from the
       file, so the top bar is hidden with height 0 and the bottom bar (it runs from its y position to the bottom of
       the screen) with `ypos r0`. Parts hidden in a window are listed with it so they can be shown again.
     - A HUD without that window's `.res` gets the game's own copy on the first change.
   - **Scoreboard:** everything it draws is under its selection:
     - **Background:** the game's picture on or off, and a colour box of the editor's own behind it (square or rounded,
       any size). The picture is a texture, so the game can't tint it.
     - **Your own row:** the highlight behind your name is the image `vgui/scoreboard/scoreboard-select`. The editor
       replaces it in the HUD with a flat colour you pick.
     - **Text colours** in groups: header, each team's name and score, each team's column titles, spectators.
     - **Player rows:** their colours are game settings (`cl_scoreboard_*_color_*`), so the HUD carries them as
       `cfg/hudeditor_colours.cfg`, run at startup by the HUD's `cfg/valve.rc` (the game's own plus that one line,
       before `autoexec.cfg`). They change in game straight away and are part of Undo, Save and discarding.
     - **Fonts:** all eleven. They live in ClientScheme (not SourceScheme). The game picks ScoreboardBody_1, then _2 and
       _3 for names too long to fit, and ScoreboardMVP for the MVP stars.
   - **Chat** reads ChatScheme: its background while typing (the game sets how see-through it is), typing text, "Say :"
     and the typing box, plus ChatFont. Clicking a part of the open chat gives that control's colours. The message
     colours (team-coloured names, yellow text) are the game's own. Its filter button, scroll bar and history can be
     clicked too, for the chat's own button and scroll bar colours. A HUD without ChatScheme.res gets one that `#base`s
     the game's on the first change.
   - **Hide a HUD part** with the **Hide** button next to it in "On screen now" (or untick "Show on screen"); hidden
     parts stay in the list with a **Show** button. Parts that only appear now and then (kill feed, pickup history,
     round end panel, bomb icon...) are under **Other HUD parts**. Most parts are hidden with opacity 0. The hint box,
     right-side text, weapon selection, radio menu and pickup history fade themselves in and out, so those are moved off
     screen instead, and moved back when shown.
   - Some colours aren't the HUD's to set: the centre print is always white, and jhud/top-left text take their colour
     from the server plugin. Their rows say so; only their fonts can change.
   - **Fonts:** face, size, weight, blur, outline, shadow and glow for your screen resolution, in the scheme file the
     part really uses (the game reports it). Additive (glow) text can't show black outlines or shadows, so ticking
     either one turns glow off. **Add a font file** copies a .ttf/.otf into the HUD and switches to it, with no restart.
     Every selection lists its fonts:
     - **HUD parts:** the fonts they use.
     - **Controls:** their type's default (Default for text and buttons, DefaultSmall for list headers...).
     - **Main menu:** the item font and the logo (ClientTitleFont). **Console:** ConsoleText.
     - **A whole window:** every font its controls use.

     **Font** under "This ... only" switches just that one control to another font from its scheme, written as `font`
     in the window's `.res`. **All fonts** at the bottom of the list, and search, reach every font in ClientScheme,
     SourceScheme and ChatScheme. **Every text font at once** (top of All fonts) sets one face and/or weight on all of
     them, every screen size, leaving icon and symbol fonts alone.

   "On screen now" lists what's visible; **Done** saves and goes back to it from a selection. Search looks through everything.
   A colour your HUD changed has a **Default** button that puts the game's own colour back, and **Undo** (Ctrl+Z)
   steps back through this session's changes.
5. **Done** or **Save HUD** keeps your changes. They go into the HUD straight away so the game can show them, and until
   you save, the editor keeps each changed file's saved version in `%LOCALAPPDATA%\CSSHudEditor\unsaved`. Closing the
   editor asks about unsaved changes. Discarding them, or opening another HUD, puts the saved versions back; after a
   crash that happens the next time the editor starts.

**Folder** opens your HUD's folder. The editor sorts every change into the right file for you: ClientScheme,
SourceScheme, HudLayout, the window `.res` files, and font files.

## Notes
- Windows may show "Windows protected your PC" the first time because the exe isn't signed:
  **More info > Run anyway**.
- A running game keeps some HUD values after they are taken out of a file (opacity, box shape, corners), so the
  editor always writes them out in full, e.g. `"alpha" "255"` when showing a part again.
- The test server switches off the idle kick (`mp_autokick 0`), so you can edit for as long as you like.
- A font "name" must be a real font face. If it isn't installed or in the HUD, the game quietly uses Tahoma.
- If the game was started some other way, the editor asks you to quit it and click **Launch game**: the plugin only
  loads when the editor starts the game.

## Building
`build.bat` (needs Visual Studio 2022 Build Tools). It packs:
- `../editor/index.html`
- `../hudreload/build/schemereload.dll` (run `../hudreload/build.bat` first)
- `defaults/`: the game's scheme, ChatScheme and HudLayout files, and in `defaults/ui` its window `.res` files

It uses the WebView2 SDK in `webview2/`. Checks:
- `CSSHudEditor.exe --selftest <empty folder>`: exit code 0 and `selftest.txt` = `ok`
- `editor/index.html#test` in Chrome: status says "Self-check passed."
