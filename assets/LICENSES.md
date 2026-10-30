# Asset licences

## Fonts (SIL Open Font License 1.1)

| File | Family | Licence |
| --- | --- | --- |
| `fonts/BigShouldersDisplay-*.ttf` | Big Shoulders Display, titles on covers and spines | `fonts/OFL-BigShouldersDisplay.txt` |
| `fonts/SchibstedGrotesk-*.ttf` | Schibsted Grotesk, the interface and cover lines | `fonts/OFL-SchibstedGrotesk.txt` |
| `fonts/JetBrainsMono-Regular.ttf` | JetBrains Mono, commands in error messages | `fonts/OFL-JetBrainsMono.txt` |

The other fonts in `fonts/` come from upstream Flex Launcher and keep their own licences.

## Textures and the room photograph (CC0, Poly Haven)

All from [Poly Haven](https://polyhaven.com), released under CC0 1.0 (public domain):

| File | Source |
| --- | --- |
| `textures/cloth_*.jpg` | Book Pattern (fabric) |
| `textures/oak_*_h.jpg` | Oak Veneer 01 |
| `textures/wall_*.jpg` | Beige Wall 001 |
| `env/fireplace_2k.hdr` | Fireplace HDRI |

## Icons

`youtube.svg`, `netflix.svg`, `hbomax.svg`, `vrtmax.svg`, `browser.svg`, `exit.svg`,
`jellyfin.svg` and `jellyfin_selected.svg` are generic pictograms drawn for this project and
released with it under the Unlicense. They are not the services' official logos; drop in your
own icon files and point the entries in `config.ini` at them.
The PNG icons come from upstream Flex Launcher.

## Code

`src/external/inih` is inih r58 by Ben Hoyt (New BSD, see its `LICENSE.txt`), used when the
system has no inih package. `src/external/nanosvg*` is NanoSVG (zlib licence), from upstream.
