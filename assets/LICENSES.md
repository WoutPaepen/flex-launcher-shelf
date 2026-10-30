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

The service icons carry each brand's own logo, placed on a round sticker in the brand's colours.
The logos are trademarks of their owners and are used here only to identify the app each book opens.

| File | Logo source |
| --- | --- |
| `icons/netflix.svg` | Netflix "N", [Simple Icons](https://simpleicons.org) (CC0) |
| `icons/spotify.svg` | Spotify mark, Simple Icons (CC0) |
| `icons/youtube.svg` | YouTube mark, Simple Icons (CC0) |
| `icons/hbomax.svg` | HBO Max 2025 wordmark, Wikimedia Commons `File:HBO_Max_2025.svg` (public domain) |
| `icons/vrtmax.svg` | VRT logo (2021), Wikimedia Commons `File:VRT_logo_2021.svg` (public domain) |

`browser.svg`, `exit.svg`, `jellyfin.svg` and `jellyfin_selected.svg` are pictograms drawn for this
project and released with it under the Unlicense. The PNG icons come from upstream Flex Launcher.
Any entry can point at your own icon file instead.

## Code

`src/external/inih` is inih r58 by Ben Hoyt (New BSD, see its `LICENSE.txt`), used when the
system has no inih package. `src/external/nanosvg*` is NanoSVG (zlib licence), from upstream.
