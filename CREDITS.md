# Credits and asset provenance

Shadesdeep's code is under the [MIT License](LICENSE). This file records where
every non-code asset came from and what licence it is under.

## Sprite art (SpriteCook)

| Files | Source | Licence |
|---|---|---|
| `art/spritecook/creatures-1.png`, `art/spritecook/items-1.png`, `art/spritecook/anim/*.png` | Generated with [SpriteCook](https://spritecook.ai); asset ids in [`spritecook-assets.json`](spritecook-assets.json) | MIT, as part of this project |
| `assets/sprites/*.png` | Sliced, mirrored, scaled and rimmed from the files above by `tools/slice_sprites.py` | MIT, as part of this project |

SpriteCook support confirmed in writing (2026-11-13) that its terms allow the
generated artwork in a public game repository, and that the project may be
released under MIT with that licence extended to any rights held in the
artwork:

> Yes, SpriteCook's terms allow you to include your generated artwork in your
> public game repository, subject to applicable law and the model providers'
> policies. You can release your code under MIT and extend that license to any
> rights you hold in the artwork.

The MIT grant covers whatever rights the project holds in this AI-generated
art; how much copyright exists in AI-generated images varies by jurisdiction.
The still sheets were made with the `gemini-nano-banana-2.1` model, so that
provider's usage policies also apply.

## Sounds and music

`assets/sounds/*` are synthesized from scratch by `tools/make_sounds.py` and
`tools/make_music.py`, part of this project: MIT.

## Fonts

| Files | Source | Licence |
|---|---|---|
| `assets/fonts/Cinzel.ttf` | Cinzel, © 2020 The Cinzel Project Authors ([source](https://github.com/NDISCOVER/Cinzel)) | SIL Open Font License 1.1, [`OFL-Cinzel.txt`](assets/fonts/OFL-Cinzel.txt) |
| `assets/fonts/Alegreya.ttf` | Alegreya, © 2011 The Alegreya Project Authors ([source](https://github.com/huertatipografica/Alegreya)) | SIL Open Font License 1.1, [`OFL-Alegreya.txt`](assets/fonts/OFL-Alegreya.txt) |

## Procedural art

Floors, walls, shades, the Hollow and the fallback sprites are drawn by code
(`src/tiles.c`, `src/render.c`): MIT.
