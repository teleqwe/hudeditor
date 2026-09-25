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
6. [ ] **Test values** in the Test tools drop-down: 5 HP, 1000+ HP, $0 / $16000, empty magazine, a round timer over
   10:00, bots with long names filling the scoreboard, MVP stars.
7. [ ] **Main menu and window settings** (ClientScheme / SourceScheme): main menu and logo position
   (`Main.Menu.X/Y`, `Main.Title1/2/3.X/Y`, `Main.BottomBorder`), window fade time (`Frame.TransitionEffectTime`),
   MOTD web page background (`HTML.BgColor`), tooltips, pop-ups, buy preset boxes.
8. [ ] **Fewer restarts**: new menu background picture shows straight away; buy menu layout live like team select.
9. [ ] **MVP star picture** (e.g. a flower): `hud/scoreboard_mvp` and the round-end panel's star. Needs testing.
10. [ ] **Loading screen** (`resource/LoadingDialog.res`): position, progress bar, hide lines. Tested by reloading the map.
11. [ ] **HUD check-up**: fonts not installed or not in the HUD, missing images and font files, duplicate blocks,
    settings old HUDs are missing; one-click fixes.
12. [ ] **Recolour the whole HUD**: replace one colour everywhere, or shift every colour's hue. Needs testing.
13. [ ] **Round-end panel** (`win_round.res`) and **killer panel** (`freezepanel_basic.res` + callout/health), with
    test buttons that show them.
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
