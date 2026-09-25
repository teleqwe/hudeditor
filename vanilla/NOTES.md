# Vanilla CS:S HUD files (reference, never edited by the user)

Taken from the game: `cstrike/cstrike_pak_dir.vpk` (resource/*.res, resource/ui/*.res, scripts/hudlayout.res,
hudanimations.txt, mod_textures.txt), `hl2/resource/sourcescheme.res` + `chatscheme.res`, and
`platform/resource/sourceschemebase.res` (what sourcescheme.res #bases). Extract more with
`bin/x64/vpk.exe l|x cstrike/cstrike_pak_dir.vpk`. Lines marked `// CLAUDE:` are my notes, checked in game.
Not in custom: a folder there with full vanilla files would cover every HUD that sorts after it.

## Where things come from
- Timer box: HudHintDisplay, font HudHintText, colours HintMessageFg/Bg (ClientScheme).
- Right-side text: HudHintKeyDisplay, font HudHintTextSmall, colour HintMessageFg (HudLayout TextColor ignored).
- Speed (centre print): CCenterStringLabel, font Trebuchet24 from **SourceScheme**, always white.
- jhud + top-left records: server HudMsg, font CenterPrintText (ClientScheme), colour set by the server.
- Spectator bars: `resource/ui/spectator.res` topbar/bottombarblank, colour Frame.BgColor (ClientScheme).

## Hiding
- HUD parts: CHud::Think sets visibility every frame, so `visible 0` does nothing; `alpha 0` in HudLayout works.
  Self-fading parts (hint box, key hint, weapon selection, radio menu, pickup history) animate alpha: move off screen.
- Window parts: the game sets their visibility too; move off screen (xpos 9999). Spectator top bar: tall 0.
  Bottom bar: ypos r0 (the game stretches it from ypos to the screen bottom).
- A running game keeps values that were removed from a file: always write them out (alpha 255, not no key).

## Scoreboard (checked in game)
- All its fonts are ClientScheme's. The .res names most; the game uses ScoreboardBody_1, then _2/_3 for long names,
  and ScoreboardMVP.
- Player rows ignore the .res (the CTPlayerName0... blocks are templates; fgcolor_override there does nothing).
  Row colours are archived client cvars cl_scoreboard_{ct,t,dead,clan_ct,clan_t,dead_clan}_color_{red,green,blue}.
  Game defaults are CT 150 200 255, T 240 90 90, dead 125 125 125, clans the same. config.cfg only lists values that
  differ from the default.
- ScoreboardBackground is a ScalableImagePanel texture; "drawcolor" does nothing. A new block in scoreboard.res
  (EditablePanel, paintbackground 1, paintbackgroundtype 2, bgcolor_override, zpos -1) is created live and draws
  behind everything.
- A scheme border (ClientScheme Borders) on a new EditablePanel block ("border" key) draws around it; after a
  clean start the scoreboard row templates (CTPlayerArea...) stay hidden, but switching HUDs live once showed them.
- hud_reloadscheme closes the scoreboard. +showscores / -showscores from the console open and close it; they need
  the client's command buffer (ServerCommand says "Unknown command").

## Chat
- Chat reads resource/ChatScheme.res (scheme tag "ChatScheme"). The background while typing is ChatScheme
  "DullWhite", with the alpha set by the game. The input text is "Chat.TypingText", and "Say :" is Label.TextColor.
  Messages use the game's colours (\x03 team colour, \x04 green).
- The ChatFont in ClientScheme is for radar labels, not the chat.
- `messagemode` from the command file doesn't open the chat (the game isn't the active window). The chat key
  pressed over the preview does, and the chat stays open after the editor takes the focus back.

## Main menu reload (GameUI)
- Refreshing BaseGameUIPanel runs each CGameMenuButton's ApplySchemeSettings: font MainMenuFont/MenuLarge (12 px,
  not proportional) and item height MainMenu.MenuItemHeight unscaled, and the logo buttons end at alpha 0. At startup
  the menu used ClientTitleFont, a proportional item font and height 66 at 1440p. CBasePanel only fades the logo in
  when a dialog opens or closes.
- The plugin keeps the logo alpha, the item fonts and Menu item height across a reload (vtable slots from
  hudreload/src/vgui_slots.cpp; the SDK 2013 headers match GameUI.dll x64: Label::SetFont at 0x710).
- The buttons come from resource/GameMenu.res (numbered blocks: label, command, OnlyInGame), read once at startup:
  scheme_reload doesn't rebuild the menu. Labels that aren't #tokens show as typed (the game's own are upper case).
  "engine connect ip:port" as the command makes a join-server button.

## Server browser (checked in game)
- Its tabs read servers/InternetGamesPage.res (CustomGamesPage.res for Internet) from platform_misc.vpk, but a copy in
  a custom folder wins. Read when the game starts. The Filters button is the "Filter" block.
- The list of servers ("gamelist", CGameListPanel) gets its font from code: ListSmall if the scheme has it, else
  DefaultSmall. A "font" in the .res doesn't reach it.

## MOTD (resource/UI/TextWindow.res)
- The frame ("info") is 640 wide in 480-line units from x 0, i.e. 4:3 from the screen's left edge, so on a wide
  screen the message sits left of centre (at 1440p: x 228 to 1668 of 2560). "xpos" "c-320" centres it.
- The message is TextMessage for a text MOTD, HTMLMessage for a web page one; MessageTitle is the title.

## Chat layout (checked in game)
- resource/UI/BaseChat.res (the game has none) is applied on every scheme reload: blocks HudChatHistory,
  ChatInputLine, ChatFiltersButton by name. Positions and widths are used; "f" widths count from the screen, not the
  chat. The typing line's y is set by code (46 px above the chat's bottom at 1440p) and the history's height runs
  down to it. The chat's background is DullWhite's colour at alpha 127 (code), the history's black at alpha 90.
- ScrollBar.Wide is in 480-line units in ChatScheme (4 = 12 px at 1440p).
- Scheme reloads and HUD reloads take the chat out of typing (its keyboard input is switched off). The plugin's panel
  list marks panels that take keyboard input ("k"); HudChat has it only while typing. The editor presses the
  player's messagemode key again when a reload closed it.

## Startup cfg from a HUD (checked in game)
- cfg/valve.rc (the game's copy is in the VPK) is run at startup from the first folder that has one, custom folders
  included, after config.cfg. A HUD's copy with "exec hudeditor_colours.cfg" added before "exec autoexec.cfg" sets
  archived cvars (the scoreboard name colours) that beat config.cfg, while the player's autoexec still wins.
- The scoreboard's row templates (CTPlayerArea, TPlayerName0...) are hidden by the game once; a live reload's
  visibility restore showed them. The plugin hides them again after every reload.

## Which font draws what in menus and in-game windows (checked in game, one font at a time given its own face)
- SourceScheme (menus): **Default** tabs, buttons, labels, check boxes, drop-downs, the Options keyboard list's rows;
  **DefaultSmall** the server browser's list of servers and every list's column headers; **DefaultVerySmall** the
  keyboard list's section headers (MOVEMENT...) and slider captions (Low/High); **UiBold** window titles
  ("Servers", "OPTIONS"); **MenuLarge** the main menu; **ConsoleText** the console's text. DefaultBold, DefaultLarge,
  ServerBrowserTitle and ServerBrowserSmall showed nowhere in the server browser or Options.
- The game's FrameTitleBar.Font is listed twice ("UiBold", then "DefaultLarge"): the first one counts.
- ClientScheme (in-game windows): **MenuTitle** window titles (MOTD, class and buy menus, from their .res), **Default**
  their buttons and labels, **DefaultSmall** the class menu's description (infolabel, set by the game).
- A reload while a map loads crashed the plugin's panel refresh; it now waits for the loading screen to go.

## Buttons in windows (checked in game)
- A Button's PerformLayout sets its colours from its state every layout, so `fgcolor_override`/`bgcolor_override` in its
  .res block do nothing. Per button: `defaultFgColor_override`, `defaultBgColor_override`, `armed...`, `depressed...`
  (and `selected...`); alpha works. Team select's Auto Assign is drawn armed (highlighted) while the menu is open.
- The team and class menus and the MOTD read their .res once. The plugin gives each existing control its block again
  on a reload (Panel::ApplySettings through the vtable; `labelText`/`text` left out so the game's texts stay).
  EditablePanel::LoadControlSettings instead deletes and remakes the controls the file made, which the game still
  points at: the next map load crashed in client.dll. A class/team button's ApplySettings reloads its info page
  (classes/<name>.res), which deletes panels, so the reload puts visibility back by walking the live tree.
- At startup the game reads the .res of whatever HUD sorts first in custom; the editor's HUD only replaces it on the
  first reload.
- New controls in those windows: the plugin makes blocks named `hudeditor...` that have no panel yet the way
  BuildGroup::NewControl does (the window's CreateControlByName, SetParent, AddActionSignalTarget(window), then
  ApplySettings), and deletes them (IVGui::MarkPanelForDeletion) when their block goes. A Button's "command" goes to
  the team/class menu's OnCommand, which runs it and closes the menu: `say !rtv` showed in chat.
- `schemereload_press <name>` (plugin command) clicks a button, for tests without the mouse.

## Added controls and sv_pure (checked in game)
- HudLayout blocks with a ControlName (ImagePanel, Label, EditablePanel) are made by the game on every HUD reload,
  as children of the viewport. A Label takes fgcolor_override, font, textAlignment; an ImagePanel image (relative to
  materials/vgui), scaleImage, drawcolor; an EditablePanel paintbackground, PaintBackgroundType, bgcolor_override.
- A multi-frame VTF (BGRA8888) with the AnimatedTexture proxy animates in an ImagePanel.
- sv_pure can't be tested on the test server: with sv_pure 2 and a map change, the host still loads a HUD's own
  materials (vgui/hudeditor and vgui/replay/thumbnails alike). By the game's cfg files: sv_pure 0 checks only
  pure_server_minimal.txt (vgui/white, sprites/white, scope_arc, flashbang, smoke...), 1 and 2 use
  pure_server_full.txt, which has `materials\... trusted_source`. Unlike TF2's, it has no replay/thumbnails exception,
  so on sv_pure 1/2 servers every HUD material (pictures, square corners, scoreboard highlight) is the game's own;
  .res files, scripts and fonts aren't listed and load from the HUD.

## HudLayout settings (checked in game)
- HudHintDisplay: text_xpos/text_ypos are the space round the text inside the box (both sides). center_x/center_y
  move only the box, away from the text (the text stays centred). HintSize is the box's width as a fraction: 0 = no box.
- HudDamageIndicator: DmgColorLeft/Right and dmg_* do nothing (Half-Life 2's). The arrows are the pain_* pictures
  from mod_textures.txt; alpha 0 hides them.
- HudHealth icon_ypos/digit_xpos move the icon/number inside the box, clipped by the box's width.
