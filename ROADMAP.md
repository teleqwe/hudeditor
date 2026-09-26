# Roadmap

Decided 2026-09-26. Ordered for someone who plays surf/bhop: what they see and use most comes first.
Each step is committed on its own, so work can stop after any of them.

## Now, in this order
1. [x] **More HUD part settings** (HudLayout keys the editor doesn't show yet)
   - Timer box and right-side text: text padding (`text_xpos/ypos`). Checked in game: `center_x/y` only move the box
     away from its text and `HintSize` is the box width, so those are left out.
   - Voice list: avatars, friend and dead icons on/off, row size and spacing, fade times
   - Weapon selection: box sizes and gap, number/icon/text positions, grow time, scroll sound
   - Kill feed: number of lines, line spacing, left/right side, font
   - Inside the number boxes: icon and number positions (health, armour, timer, ammo and its bar, money and its +/- line)
   - Damage indicator: its HudLayout colours/sizes do nothing in CS:S (checked); the arrows can be faded or hidden.
     Recolouring them means swapping their pictures (`pain_*` in mod_textures), like the MVP star.
   - Per-part colours: low health, timer flash, icon colours (so they don't share the warning red / highlight green)
   - Smaller: pickup history spacing, plant/defuse bar border, scope settings
2. [x] **Command buttons** in windows (team and class select; the buy menu comes with step 15): an "Add a command button" section after Borders. The new
   button gets a sensible size and copies another button's colours and border. Presets for surf/bhop/ZE servers.
   The plugin creates new buttons live.
3. [x] **+ Add to HUD**: pictures (PNG/JPG), animated pictures (GIF), text and colour boxes, in HudLayout
   - **sv_pure check**: the test server can't show it (its own player isn't checked). The game's cfg files say sv_pure
     1/2 block every custom material, replay/thumbnails included (TF2 has an exception for that folder, CS:S doesn't).
     The editor says so under Add to the HUD. Checking on a real sv_pure server is still to do.
4. [x] **Nudge and align**: arrow keys move the selection 1 unit (Shift: 10), Ctrl+click several parts to line them
   up or spread them, a 4:3 safe-area overlay.
5. [x] **Spectator extras**: `spectator.res` (scores, timer, info line, title) and `bottomspectator.res` (drop-downs,
   previous/next buttons).
6. [x] **Test values** in the Test tools drop-down: 5 HP, 1500 HP, $0 / $16000, empty magazine, a 15:00 round timer,
   bots with long names filling the scoreboard, kill feed, reset. Still to do: MVP stars (the count isn't a networked
   field the plugin can set; comes with step 9).
7. [x] **Main menu and window settings** (ClientScheme / SourceScheme): main menu and logo position
   (`Main.Menu.X/Y`, `Main.Title1/2.X/Y`, `Main.BottomBorder`, live), window fade time (`Frame.TransitionEffectTime`),
   MOTD web page background (`HTML.BgColor`), tooltips. Buy preset boxes go with step 15; `Popup.BgColor` and
   `Main.Title3` weren't found drawing anything, so they stay under All other colours.
8. [x] **Fewer restarts**: a new menu background picture shows straight away (and opening or discarding a HUD shows its own).
   The buy menu going live moved to step 15, which needs the same look at how its pages are made.
9. [x] **MVP star picture** (e.g. a flower): `hud/scoreboard_mvp` and the round-end panel's star, live. Checked in game
   with both materials on screen (flower, then the game's star back). Not yet seen on a real scoreboard row: the test
   server can't give MVPs yet.
10. [x] **Loading screen**: the game really reads `LoadingDialogNoBanner.res` (VAC servers: `LoadingDialogVAC.res`),
    not `LoadingDialog.res`. Show/hide text, bar and Cancel; box size; parts' places. Checked on a map reload (the test
    server turns vgui_cache_res_files off so each load reads the file). The game centres the box.
11. [x] **HUD check-up**: fonts not installed or not in the HUD, missing font files and replay/thumbnails pictures,
    blocks written twice, the game's fonts/colours/borders/settings an old HUD leaves out; fixes where there's one.
    Tried on a copy of Gasai GUI: 8 findings, 0 after its fixes. Not checked: pictures outside replay/thumbnails (they
    may come from the game's files, which the editor can't list).
12. [x] **Recolour the whole HUD**: replace one colour everywhere, or shift every colour's hue. Checked in game on
    testhud: a 120° shift turned the timer text from green to purple live (55 of 151 colours changed; greys kept).
13. [x] **Round-end panel** (`win_round.res`) and **killer panel** (`freezepanel_basic.res`), with test buttons that
    show them (the plugin fires cs_win_panel_round + round_mvp, show_freezepanel, hide_freezepanel/round_start).
    Checked in game: fun fact and screenshot hint hidden live. Not done: the freeze-cam callout (freezepanelcallout.res)
    and the killer's health (freezepanelkillerhealth.res); the killer's name shows "[unknown]" for the test bot.
14. [ ] **C4 panel** (`resource/c4panel.res`): colours of the bomb's screen.
15. [ ] **Buy menu**: all its pages (`buymenu_ct/ter`, sub-menus, `classes/*.res` weapon pages, `loadout.res`).
16. [ ] **Preview what Save writes** (low priority): every change per file, old → new, each with an undo.

## Later
- **Addon merger** (back burner): "Add to my HUD" from any HUD/addon download, copying the parts and everything they
  need (colours, fonts, borders, images), renaming clashes.
- **Update check** (not yet): at startup, see if GitHub has a newer `app/CSSHudEditor.exe`.

## No for now
- Animation tricks / animations editor (`hudanimations.txt`: loops, low-health pulse, fade timings)
- Custom shaders (gradients, glow, blur)
- HUD versions shipped as full copies (colour/font/aspect variants)

## No
- Live speed / speedometer builder (the server shows speed)
- Survive game updates (vtable checks at startup, in-game smoke test)
- HUD browser (install HUDs from GameBanana inside the editor)
- Styled radar (round/tinted map overviews)
- Readability check on real maps

## Not answered yet
- **commandmenu.res** ("Commandment"): an old pop-up menu the game opens with a key bound to `+commandmenu`. Its items
  are buttons that run commands (vanilla has "Team menu", "Drop weapon", radio messages). A HUD could fill it with its
  own commands, e.g. `!r`, `!rtv`, `!spec`, as a key-held menu. Worth it? (Needs a check that CS:S still opens it.)
- `spectatormenu.res` / `spectatormodes.res`: the same kind of menu for spectators.
- The end-of-map panel (`win_match.res`), achievement pop-ups, and the stats/achievement dialogs' green.
- Beyond `.res`: several menu backgrounds (`ChapterBackgrounds.txt`), renaming game texts (`cstrike_english.txt`),
  and HUD cvars in the startup cfg (`hud_deathnotice_time`, `hud_saytext_time`, `cl_radaralpha`).
