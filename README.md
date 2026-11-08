# Lume

A graphical roguelike played **inside the kitty terminal**: real RGBA pixel
art, per-pixel coloured lighting and smooth animation, presented through
kitty's graphics protocol. Brogue-style accessibility: full mouse support,
hover descriptions and clickable actions.

Version: 0.5.1 (cut-down front walls, side doors, quieter drips, sleepy tooltips) · Linux · needs [kitty](https://sw.kovidgoyal.net/kitty/)

## Build and run

```sh
make            # builds build/lume and symlinks ./lume
./lume          # run inside kitty
```

Needs a C compiler (GNU C11), Make, pkg-config, and the libpng, zlib,
FreeType and SDL3 development packages
(`sudo pacman -S base-devel libpng zlib freetype2 sdl3`). SDL3 is only used for
sound; if no audio device opens, the game runs silent.

## Playing

- **Mouse:** hover anything for a description; click to travel (the path is
  previewed), click a monster to attack it, click a cold sconce to walk over
  and light it, click yourself to rest. The wheel zooms. Descriptions fade
  after a few seconds, and pressing a key hides the hover marker until the
  mouse moves again.
- **Keys:** arrows / numpad / `h j k l y u b n` move and attack; `x` explore;
  `z` or `.` rest a turn, `Z` rest until healed; `>` travel to the stairs
  down and descend, `<` climb; `i` items, `t` throw, `a` / `q` use,
  `d` drop, `T` light or snuff your torch; `m` map; `+` / `-` zoom;
  F2 sound on/off; `?` help; Esc twice quits.
- **Map:** `m` (or the Map button) shows the whole explored level, drawn
  with the game's own graphics: you, the stairs and any creatures you can make
  out are marked. Click a place on it to travel there.
- **Sound:** footsteps, splashes, steel, the creatures' voices, torches
  catching and hissing out, a low dungeon ambience, and crackling fire and
  lapping water that rise as you get near. Sounds are panned by where they
  happen and muffled out of sight. `--mute` starts silent, `--no-sound` never
  opens the audio device.
- **Items:** every action has an on-screen button. Press Throw (or `t`) and
  your throwable items are listed; pick one, then aim with the mouse or the
  direction keys (Tab cycles visible monsters) and click or press Enter.
  Darts hit hard and can usually be picked up again; potions heal.
- **Darkness:** some rooms start unlit. Lighting a sconce shows you the room,
  but anything in the light can see you too. In the dark, monsters only notice
  you when you are close.
- **Torches:** a lit torch lights a wide circle around you but burns out and
  makes you visible from afar. Throw one to light a room from a distance (it
  burns on the floor for a while), or to lure monsters with the noise. Walk
  onto a burning torch with your hand free to take it up again; one that
  burns out leaves a charred stub.
- **Shades** (from depth 2) are living darkness. They gather in unlit rooms,
  hunt you by your warmth whether you are lit or not, and in the dark you see
  only their eyes. They cannot enter firelight (sconces, burning torches, a
  torch in your hand; the cold glow of crystals and moss doesn't count), and
  light burns them. Darts pass straight through; a thrown torch sears them.
  So light keeps shades away while it shows you to everything else.
- **Water:** wading through the shallows splashes, and the noise carries.
  Deep water must be swum: it puts out (and ruins) a lit torch, so you are
  asked twice before swimming with one, and anything dropped in it sinks.
  Goblins won't follow you in. A torch thrown into water hisses out, and a
  chasm over water makes for a softer landing.
- **Chasms:** you can see the real level below through them. Stepping in asks
  whether to jump: it is a quick way down, but the fall hurts. Anything you
  throw into a chasm lands on the level below, and a burning torch keeps
  burning down there.

Text size follows kitty's own font size (ctrl+shift+= / -), or set `--text`.
The pixel zoom is picked from the window height; override it with `--zoom` or
`+` / `-`. `./lume --help` lists every option.

## Development

`./lume --dump out.png [--seed N] [--size WxH] [--cell H] [--steps N]` renders
a frame to PNG without a terminal. `LUME_DUMP_CHASM=1` stands at the edge of a
chasm with the level revealed, `LUME_DUMP_JUMP=N` jumps down N chasms, and
`LUME_DUMP_LOWER=file.png` writes the rendered level below. `LUME_DUMP_TORCH=1`
lights a torch, `LUME_DUMP_MENU=inv|throw|use|item` opens a menu,
`LUME_DUMP_AIM=dx,dy` aims a torch and `LUME_DUMP_THROW=dx,dy,frac` freezes a
thrown torch part-way through its flight. `LUME_DUMP_REST=N` waits N turns
first (e.g. to let a thrown torch burn out). `LUME_DUMP_SHADE="dx,dy;dx,dy"`
puts shades near the player. `LUME_DUMP_WATER=0|1|2` stands by a pool, in its
shallows, or swimming. `LUME_DUMP_SPOT=0|1|2` stands in an east-west corridor
(with a rat and darts beside you), beside a door in a side wall, or just north
of a room's south wall. `LUME_DUMP_MAP=1` opens the map.

The sounds are synthesized by `tools/make_sounds.py` (Python 3 with numpy and
scipy) into `assets/sounds` (files already there are kept, so sounds you swapped in survive; `--only
NAME...` re-renders just those, `--force` re-renders everything); `assets/sounds/sounds.txt` says which file(s)
each event plays and how loud, and is only written when missing, so edit it
freely. `python3 tools/make_sounds.py --only step door` re-renders just those.

The tiles and sprites are procedural placeholders (`src/tiles.c`) until the
real art arrives. Fonts: Cinzel and Alegreya (SIL Open Font License, see
`assets/fonts/`).
