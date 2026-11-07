# Lume

A graphical roguelike played **inside the kitty terminal**: real RGBA pixel
art, per-pixel coloured lighting and smooth animation, presented through
kitty's graphics protocol. Brogue-style accessibility: full mouse support,
hover descriptions and clickable actions.

Version: 0.1.0 (first playable slice) · Linux · needs [kitty](https://sw.kovidgoyal.net/kitty/)

## Build and run

```sh
make            # builds build/lume and symlinks ./lume
./lume          # run inside kitty
```

Needs a C compiler (GNU C11), Make, pkg-config, and the libpng, zlib and
FreeType development packages (`sudo pacman -S base-devel libpng zlib freetype2`).

## Playing

- **Mouse:** hover anything for a description; click to travel (the path is
  previewed), click a monster to attack it, click a cold sconce to walk over
  and light it, click yourself to rest. The wheel zooms.
- **Keys:** arrows / numpad / `h j k l y u b n` move and attack; `x` explore;
  `z` or `.` rest a turn, `Z` rest until healed; `>` travel to the stairs
  down and descend, `<` climb; `+` / `-` zoom; `?` help; Esc twice quits.
- **Darkness:** some rooms start unlit. Lighting a sconce shows you the room,
  but anything in the light can see you too. In the dark, monsters only notice
  you when you are close.
- **Chasms:** you can see the real level below through them. Stepping in asks
  whether to jump: it is a quick way down, but the fall hurts.

Text size follows kitty's own font size (ctrl+shift+= / -), or set `--text`.
The pixel zoom is picked from the window height; override it with `--zoom` or
`+` / `-`. `./lume --help` lists every option.

## Development

`./lume --dump out.png [--seed N] [--size WxH] [--cell H] [--steps N]` renders
a frame to PNG without a terminal. `LUME_DUMP_CHASM=1` stands at the edge of a
chasm with the level revealed, `LUME_DUMP_JUMP=N` jumps down N chasms, and
`LUME_DUMP_LOWER=file.png` writes the rendered level below.

The tiles and sprites are procedural placeholders (`src/tiles.c`) until the
real art arrives. Fonts: Cinzel and Alegreya (SIL Open Font License, see
`assets/fonts/`).
