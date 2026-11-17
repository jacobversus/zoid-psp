<p align="center"><img src="docs/logo.png" alt="ZOID" width="360"></p>

# ZOID for PSP

A homebrew media app for the Sony PSP: music (MP3, FLAC, WAV, M4A/AAC), videos, internet radio, podcasts, and manga and comics, in nine themes. Everything is drawn by the PSP's GPU at 60 fps, and audio and video use the PSP's hardware decoders wherever they can.

![The Home screen in all nine themes](docs/screenshots/01-home-all-nine-themes.png)

## Features

- **Music**: MP3, FLAC, WAV (24-bit and high sample rates included) and M4A/AAC. Gapless playback, crossfade, trim silence, album/disc/track sorting, shuffle all, playlists, cover art, a 10-band equalizer, synced lyrics (from `.lrc` files or LRCLIB), 25 visualizers and resume on start.
- **Videos**: H.264 `.mp4` decoded by the Media Engine, with subtitles, zoom and skips. A Windows converter is included.
- **Internet radio**: search Radio Browser or browse by genre, popularity and local stations, plus iHeartRadio's AM/FM directory. HTTPS streams work through BearSSL.
- **Podcasts**: Apple's directory, subscriptions, new episodes, offline saving and per-episode resume.
- **Books**: `.cbz` files and folders of pages, right-to-left manga reading, smooth zoom, upright reading, bookmarks and progress.
- **Themes**: Zoid, Cyberpunk, Phosphor, Y2K, Studio Rack, Paper, Zoidstation 1, Zoidstation 2 and Zoidstation Portable. Each has its own layout and fonts, not just different colors.
- **Battery**: screen off on HOLD, a 222 MHz mode, or keep your CFW's overclock. Wi-Fi turns itself off when idle.

![Now Playing in every theme](docs/screenshots/02-now-playing-all-nine-themes.png)

## Screenshots

All screenshots are the PSP's own 480x272 screen, scaled 2x.

### Now Playing

| Cyberpunk | Y2K |
|---|---|
| ![Now Playing in Cyberpunk](docs/screenshots/now-playing-cyberpunk.png) | ![Now Playing in Y2K](docs/screenshots/now-playing-y2k.png) |
| **Studio Rack** | **Paper** |
| ![Now Playing in Studio Rack](docs/screenshots/now-playing-studio-rack.png) | ![Now Playing in Paper](docs/screenshots/08-paper-now-playing.png) |

### Visualizers

25 visualizers, up to three at once. Press X on a panel to pick another.

| | |
|---|---|
| ![Spectrum bars, starfield and ridgelines](docs/screenshots/visualizers-bars-starfield-ridgelines.png) | ![Wave ribbons, orbit and rain](docs/screenshots/visualizers-wave-ribbons-orbit-rain.png) |
| ![Synth grid, bubbles and sunburst](docs/screenshots/visualizers-synth-grid-bubbles-sunburst.png) | ![Heartbeat, spectrum bars and mirror bars](docs/screenshots/visualizers-heartbeat-spectrum-mirror.png) |

![24 of the visualizers](docs/screenshots/03-visualizers.png)

### Books

The library shows every series and volume with its reading progress, in whichever theme you pick.

![The Books library in four themes](docs/screenshots/books-library-four-themes.png)

| A series and its volumes | Paper theme |
|---|---|
| ![A series with per-volume progress](docs/screenshots/books-series-progress.png) | ![The Books library in Paper](docs/screenshots/13-books-shelf.png) |

### Internet radio and podcasts

| A station playing | The same in Cyberpunk |
|---|---|
| ![A radio station playing](docs/screenshots/radio-playing.png) | ![A radio station playing in Cyberpunk](docs/screenshots/radio-playing-cyberpunk.png) |
| **Popular stations** | **Lyrics for the song on air** |
| ![A list of stations](docs/screenshots/radio-stations.png) | ![Lyrics following a radio station](docs/screenshots/10-radio-live-lyrics.png) |
| **A podcast episode playing** | **Episodes, saved for offline** |
| ![A podcast episode playing](docs/screenshots/12-podcast-playing.png) | ![A podcast's episodes in Cyberpunk](docs/screenshots/podcast-episodes-cyberpunk.png) |

### More

| Songs in any language | 10-band equalizer |
|---|---|
| ![Song names in many languages](docs/screenshots/05-library-world-names.png) | ![The equalizer](docs/screenshots/06-equalizer.png) |
| **Synced lyrics (Phosphor)** | **Theme colors** |
| ![Synced lyrics in Phosphor](docs/screenshots/07-lyrics-phosphor.png) | ![Themes in other colors](docs/screenshots/04-theme-colors.png) |

All screenshots are in [docs/screenshots](docs/screenshots).

## Install

Download the latest zip from the Releases page and copy its `PSP` folder to the root of your Memory Stick, so you have `PSP/GAME/ZOID/EBOOT.PBP` and `PSP/GAME/ZOID/zoidvol.prx`. Custom firmware is required. It's tested on a PSP-3000 with ARK-5, and PSP Go internal storage works too.

Put music in `MUSIC`, videos in `VIDEO` and books in `BOOKS` on the root of the Memory Stick. The `README.txt` in the zip covers controls, file names and the companion tools.

## Companion tools (Windows)

The zip's `Tools` folder has drag-and-drop helpers for getting files ready. Run **Add Desktop Shortcuts** once to put them on your Desktop.

| Tool | What it does |
|---|---|
| **Convert for PSP** | Drop videos on it: makes 480x272 `.mp4` files with the soundtrack and subtitles inside. Uses Windows' own encoder, or FFmpeg if installed. |
| **Shrink Books for PSP** | Drop books on it: makes copies about a quarter of the size, and turns `.cbr` into `.cbz`. |
| **Fix PSP Names** | Renames files on the Memory Stick that a PSP can't open (accents, `½`, Japanese), keeping bookmarks and playlists working. |
| **Check Video for PSP** | Drop an `.mp4` on it to see whether ZOID can play it. |
| **Install Helpers** | Offers to install 7-Zip (for `.cbr` books) and FFmpeg (smaller, sharper videos) through winget. |
| **HandBrake presets** | `PSP (ZOID) - English` and `- Japanese`, for converting in HandBrake instead. |

They're in [tools](tools), laid out exactly as in the zip.

## Building

You need Windows and the PSPDEV toolchain.

1. Download the Windows build of the [PSPDEV toolchain](https://github.com/pspdev/pspdev/releases) and unzip it as `pspdev-win` next to this repository's folder (or pass `-Toolchain <path>`).
2. In PowerShell, run `scripts/build.ps1`. It builds BearSSL the first time, then `out/EBOOT.PBP` and `out/zoidvol.prx`.
3. `scripts/make-release.ps1` builds the release zip in `out/`.

| Folder | What's in it |
|---|---|
| `src/` | The app. `src/fonts` holds the pre-rendered theme fonts, `src/zoidvol` the small kernel helper, and `src/third_party` BearSSL, dr_flac and dr_wav. |
| `tools/` | The companion tools, exactly as they ship in the zip. |
| `scripts/` | Build, release, and the generators for the icon and fonts. |
| `assets/` | The XMB icon and background. |
| `docs/` | The [technical guide](docs/technical-guide.md), which covers every screen, file format and control, plus the screenshots. |

## Credits

- [BearSSL](https://bearssl.org) by Thomas Pornin (MIT) for HTTPS
- [dr_flac and dr_wav](https://github.com/mackron/dr_libs) by David Reid (public domain or MIT-0)
- Station directory from [Radio Browser](https://www.radio-browser.info), lyrics from [LRCLIB](https://lrclib.net), and podcasts from Apple's iTunes Search API
- Built with [PSPDEV](https://github.com/pspdev)

ZOID isn't affiliated with Sony, iHeartMedia or Apple. The iHeartRadio directory is the one iHeart's own website uses; it isn't an official public API, so it may change.

## License

MIT. See [LICENSE](LICENSE). Bundled third-party code keeps its own license.
