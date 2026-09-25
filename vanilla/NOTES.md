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
