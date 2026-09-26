# CS:S HUD Editor

One file, `CSSHudEditor.exe`, with nothing to install (download it from this folder: `app/CSSHudEditor.exe`): the live-reload plugin, the editor and the default files are
packed inside it. Needs Windows 10 or 11 (it uses the WebView2 runtime that ships with Windows 11).

## Use
1. Quit Counter-Strike: Source and open `CSSHudEditor.exe`. It starts the game windowed, with the plugin, and shows
   the game window in the editor on its own. The editor captures the game's window directly (Windows.Graphics.Capture):
   no share dialog, and nothing else on screen is looked at.
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
   bhop/surf-style texts on screen. **Test tools** beside it (it opens when a test game starts)
   holds the switches for each one (remembered), plus Scoreboard and Chat, which keep those open (the chat is
   opened again with your chat key after a change reloads the HUD):
   - **Timer:** the timer hint box (HudHintText) and the right-side text (HudHintTextSmall). The hint's beep is
     silent while the editor runs: the plugin has the game use a silent `sound/ui/hint.wav` from
     `addons/schemereload_sounds`. Nothing goes into your HUD.
   - **Small speed:** PrintCenterText, drawn with font Trebuchet24 from SourceScheme
   - **Large speed:** ShowHudText/game_text just under it (jhud), drawn with font CenterPrintText from ClientScheme
   - **Top left:** record times in the top left corner, also ShowHudText with CenterPrintText
   - **Chat:** a chat line every 3 seconds (a player in team colour and a green `[Timer]` line), font ChatFont from
     ChatScheme

   Rounds never end on the test server (`mp_ignore_round_win_conditions 1`), so windows don't close mid-edit.
   **Test values** (also in Test tools) put the HUD's numbers at awkward values: 5 HP (the low health colour), 1500 HP
   (four digits: the game's own box cuts the last one off), $0 and $16000, an empty magazine, a 15:00 round timer (over
   10:00, as surf/bhop servers run; starts the round again), a full scoreboard of bots with long names, the kill feed
   filled (every bot killed), and Reset (one frozen bot, 9:00). The plugin sets them on the test server only.

   **Scoreboard** keeps the scoreboard open until you click it again, and opens it again when a change reloads the
   HUD. Keys you press with the mouse over the preview (Esc, ~, B...) go to the game. Your chat key opens the chat,
   and it stays open for editing. The game comes to the front while the key is down and the editor takes the front back
   once you let go (a map otherwise locks the mouse inside the game).
   **Drag anything in the preview to move it**, or press the arrow keys (1 unit, 10 with Shift). **Ctrl+click** more
   parts to move them together and to line them up with the first one (left edges, centres, tops...) or spread them
   evenly. **4:3 edges** in Test tools draws where a 4:3 screen's picture ends. HUD parts move in HudLayout, window controls in the window's `.res`
   (`xpos`/`ypos`, keeping `r` and `c` anchors). **Centre** beside X and Y position centres it (in its window, for a
   window's control: `c` and `r` count from the screen even there, so inside a window smaller than the screen it writes
   a plain number from the window's edge), and **Undo** there puts it back where it was when the HUD was opened. Dragged near an edge of the screen (a window's control: of its
   window) or its middle, a part snaps there, stopping 1 unit short of the edge (positions are whole units of screen
   height / 480: 3 px at 1440p). The dashed box shows where it goes, and the game follows once you
   pause. What the game places itself (server browser, window frames) can't be moved, the main menu only with its position rows, and the radio menu
   only moves up and down (the game keeps it full width). The same X and Y positions are rows on every part that has
   them. The buy menu reads its own settings at startup, so changes to its controls show after a restart; the plugin reads the team and class menus' and the MOTD's again on every change.
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
     - **Settings** (under More): what a part's own code reads from HudLayout. Where the icon and number sit inside
       health, armor, timer, ammo (and its divider) and money (and its +/- line); the space round the timer text; the
       right-side text's place; weapon selection's box sizes, gaps, positions, grow time and scroll sound; the kill
       feed's number of lines, line height, side and font; the voice list's avatars, icons, sizes and fade times; the
       pickup history's spacing; the plant/defuse bar's border. Colours for one part only: low health, the timer's
       flash, the buy zone, bomb, defuse kit and rescue zone icons.
     - **Damage arrows** (under Other HUD parts) can be faded or hidden. They are pictures: HudLayout's DmgColor and
       dmg_ settings are Half-Life 2's and do nothing in CS:S.
   - **Round-end panel and killer panel:** **Round end** and **Killer panel** in Test values show them on the test
     server (a CT win with you as MVP and a fun fact; a bot as your killer), **Hide panels** takes them away. Click
     their parts for their own colours, fonts and places. The round-end panel (`resource/UI/Win_Round.res`) has its
     three boxes' colours and show/hide for the title, how the round was won, timer, MVP, fun fact and each box; the
     killer panel (`FreezePanel_Basic.res`) its box and edge colours, each text's colour, and show/hide for the texts,
     avatar, health bar and domination icon. Both read their file once, so the plugin gives their parts their
     settings again on every change.
   - **Recolour the HUD** (below On screen now): every colour the HUD sets, most used first. Click one to change it
     everywhere it's used (each place keeps its opacity), or **Shift every hue** by up to 180° (greys and white stay).
     That covers the schemes' named colours and colour settings (the game's defaults too, so a HUD that #bases them
     recolours as well), borders' lines, and colours written in HudLayout and window files. One Undo per file.
   - **HUD check-up** (below On screen now): **Check the HUD** lists what quietly goes wrong, each with a fix where
     there is one: font faces this PC doesn't have and the HUD doesn't bring (the game shows Tahoma; "Verdana Bold"
     style names count as Verdana, as Windows finds them), font files CustomFontFiles names that aren't in the HUD,
     pictures under replay/thumbnails that aren't there, blocks written twice (the game reads only the first), and the
     game's own fonts, colours, borders and settings an old HUD's scheme leaves out (**Add the game's**).
   - **Loading screen** (Other screens, below On screen now): the box that shows while a map loads. Show or hide its
     text, progress bar and Cancel button; its width and height; where the parts sit inside. **See it** starts the test
     game again. The game reads `resource/LoadingDialogNoBanner.res` (and `LoadingDialogVAC.res` on VAC-secured
     servers: the same box, taller, with the VAC notice), not `LoadingDialog.res`; the editor changes both. The game
     centres the box itself; its colours are the menus' (window background, progress bar, grey text).
   - **Add to the HUD** (below On screen now): **+ Picture** (PNG, JPG, WebP, or an animated GIF, which plays in
     game), **+ Text** and **+ Colour box**. Each is added in the middle of the screen and selected: drag it, size it,
     and set its opacity, layer (in front of or behind other parts), tint or text colour, font and alignment. Pictures
     are written to `materials/vgui/replay/thumbnails` in the HUD. Servers with sv_pure 1 or 2 block every material a
     HUD brings (pictures, square corners, the scoreboard highlight); texts, boxes, colours and fonts show everywhere.
   - **Windows** (team select, buy menu, scoreboard, spectator bars, MOTD, main menu, options, console): the exact
     control you clicked. You get, in this order (colours, then fonts, then borders, then the rest):
     - its own colour and position (**Just this ...**)
     - the colours shared by every control of that type, and the window colours
     - the fonts it uses
     - its borders (normal, focused, pressed...): which sides draw (four tick boxes), thickness and colour. Sides that
       are off are written as clear lines, since a running game keeps lines a file no longer lists
     - for menu windows, square or rounded corners and the window edge
     - **Command buttons** (team and class select, under More): add a button that runs a command, such as `say !rtv`
       (suggestions for surf/bhop servers come up as you type). A click closes the menu and runs it as if typed in the
       console. The new button copies the look of the window's most customised button (colours, border, font, size)
       and goes a row under the lowest one; the plugin makes it straight away (blocks named `hudeditor_cmd1`...).
     - **Main menu position** (`Main.Menu.X/Y`, `Main.BottomBorder`, `Main.Title1/2.X/Y` in ClientScheme): where the
       menu and the logo's two lines sit, live. A menu that would reach the bottom gap is lifted, logo and all.
     - **Fading** (any window, under More): how long windows take to fade in and out, and to go behind another
       window; menus also changing tabs (`Frame.TransitionEffectTime`...). The game's menus take 0.3 s, its in-game
       windows 0. The MOTD's web page background (`HTML.BgColor`) is under its colours, and menus' tooltip colours
       under the window colours.
     - **Main menu:** hide or colour the logo (`Main.Title1.Color`, `Main.Title2.Color` in ClientScheme), the item
       height (`MainMenu.MenuItemHeight`, scaled to your screen) and the menu text colours. The item text size is the
       MenuLarge font. **Import a picture** (also found by searching "background") sets the menu background: it is
       cropped to fill the screen and written as `materials/console/background01` (4:3) and `background01_widescreen`
       (uncompressed VTFs, under names that take turns so the running game loads the new one: it shows straight away).
       **Buttons** (`resource/GameMenu.res`): remove, reorder, put back any of the game's own buttons, or add one that
       joins a server (type the IP:port). Also read at startup only.
     - **Server browser:** show or hide the Filters button (`servers/InternetGamesPage.res`, `CustomGamesPage.res`;
       read at startup). Its list of servers uses ListSmall (DefaultSmall when the scheme has none), set by
       the game.
     - **MOTD:** centre it on the screen (the game puts it 4:3 wide from the left edge), and show or hide its title,
       text and web page message, leaving only OK. Read at startup.
     - **Chat:** click its parts in the preview to move or hide them (`resource/UI/BaseChat.res`, e.g. the Filters
       button), and set the padding inside it. Hidden parts are listed under "Chat: hidden parts" to bring back.
     - **Scroll bar width** (`ScrollBar.Wide`) shows with scroll bar colours, or search "scroll", for in-game windows,
       menus and the chat.
     - **Show on screen** to hide the control. It moves off screen and its position is kept in `xpos_hudeditor`. The
       game sets window parts' visibility itself, so `visible` can't hide them.
     - **Spectator bars:** show or hide each part: the top and bottom bar (keeping their text), the title, team
       scores, timer, info line, divider and player name; in the bottom menu the three drop-downs and the
       previous/next buttons. Click any of them for its own colours, font and position. The game sizes these two from the
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
     - **MVP star:** Change picture... swaps the star beside MVPs' names and on the round-end panel for a picture of
       yours (a GIF animates); The game's star puts it back. Both show straight away.
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
   - Some colours aren't the HUD's to set: the centre print is always white, and the large speed/top-left text take their colour
     from the server plugin. Their rows say so; only their fonts can change.
   - **Fonts:** face, size, weight, blur, and outline, shadow, glow and anti-aliasing as Off/On, for your screen
     resolution, in the scheme file the part really uses (the game reports it). **+** under a font adds the settings it
     doesn't have yet (italic, underline, strike-through, scan lines, character range, symbol, rotary, custom). Additive
     (glow) text can't show black outlines or shadows, so turning either on turns glow off. **+ Add font** at the top
     of the Fonts card copies a .ttf/.otf into the HUD (no restart); then type its name into any Font box.
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
   Double-click a title, name or description in the list to rename it (kept in the editor on this PC, not in the HUD);
   ↺ next to it puts the original back. A selection's sections are in folders (Colours, Fonts, Borders,
   More) that fold like a directory, all its fonts in one card; a section's chevron folds it too (remembered). The
   settings a section changes (Frame.BgColor...) show when the mouse is over its title. Colour rows show the RGB
   over the colour and the opacity over a second swatch; either opens the colour picker (shade, hue, opacity fading
   from 100% to 0%, the numbers, and **Copy**/**Paste** as `R G B A`; Paste also takes #hex and rgb()). A button's own
   colours (Team select, class menus) are set per state: normal, mouse over, clicked.
   Messages pop up at the bottom of the list for a few seconds.
   A colour your HUD changed has a **Default** button that puts the game's own colour back. A row's **Undo** puts it
   back to how it was when the HUD was opened (a new HUD: the defaults); the top **Undo** (Ctrl+Z) steps back
   through this session's changes.
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
  loads when the editor starts the game. The editor leaves such a game alone (no capture) until then.

## Building
`build.bat` (needs Visual Studio 2022 Build Tools). It packs:
- `../editor/index.html`
- `../hudreload/build/schemereload.dll` (run `../hudreload/build.bat` first)
- `defaults/`: the game's scheme, ChatScheme and HudLayout files, and in `defaults/ui` its window `.res` files

It uses the WebView2 SDK in `webview2/`. Checks:
- `CSSHudEditor.exe --selftest <empty folder>`: exit code 0 and `selftest.txt` = `ok`
- `editor/index.html#test` in Chrome: status says "Self-check passed."
