ZOID for PSP 1.0
================

A media app for the PSP: music (MP3, FLAC, WAV, M4A/AAC), videos, internet
radio, podcasts, and manga/comics, with nine themes.

Needs custom firmware. Tested on a PSP-3000 with ARK-5. PSP Go internal
storage works too. On a PSP-1000 (32 MB of memory), Books may not have enough
memory for large pages.


INSTALL
-------
Copy the PSP folder from this zip onto the root of your Memory Stick, so the
app ends up at:

    PSP/GAME/ZOID/EBOOT.PBP
    PSP/GAME/ZOID/zoidvol.prx   (shows the volume and lets the music-note
                                 button switch themes; keep it there)

Then put your files in these folders on the root of the Memory Stick
(ef0: on a PSP Go). ZOID creates PODCASTS by itself.

    MUSIC/    MP3, FLAC, WAV, M4A. Folders are fine. cover.jpg / folder.jpg
              in a folder is used as the cover. A .lrc file beside a song
              gives synced lyrics.
    VIDEO/    .mp4 videos (see VIDEOS below)
    BOOKS/    .cbz files, or folders of .jpg/.png pages. A folder of
              volumes is shown as a series.

Internet radio, podcasts and online lyrics use the Wi-Fi networks saved in
the PSP's own Settings > Network Settings. The WLAN switch must be on.


NAMES MUST BE PLAIN ENGLISH LETTERS
-----------------------------------
A real PSP cannot open a file or folder whose name has letters such as
é, ü, ½ or Japanese characters. Song tags can use any language; only the file
and folder names are affected. After copying files to the Memory Stick, run:

    powershell -ExecutionPolicy Bypass -File Tools\fix-names.ps1 -Drive E:

(use your Memory Stick's drive letter). It renames everything in BOOKS,
MUSIC, VIDEO and PODCASTS and keeps your bookmarks and playlists working.
Add -WhatIf to see the changes first.


CONTROLS
--------
    D-pad / stick   move
    X               open / play / choose
    Circle          back (enough presses always lead Home)
    Triangle        jump between the list and Now Playing
    SELECT          Settings
    START           play / pause
    L / R           previous / next
    HOLD switch     screen off, music keeps playing

Buttons can be remapped in Settings. Hold X on a song, folder or station for
more options (sort, add to playlist, save station, and so on).

In Books: L and R turn pages (manga reads right to left, so L is next),
X zooms in, Square zooms out, SELECT switches reading direction, and
Triangle shows all controls, including upright reading.


VIDEOS
------
ZOID plays H.264 .mp4 files up to 720x480. For the best results, convert
videos with the included tool, which makes a 480x272 file and puts the
soundtrack and subtitles inside it (Windows 10 or 11):

    powershell -ExecutionPolicy Bypass -File Tools\convert-for-psp.ps1 -Source movie.mkv -Destination "PSP Videos\movie"

It uses Windows' own encoder, or ffmpeg if it is installed (smaller, sharper
files). Only DRM-free videos can be converted.


SMALLER BOOKS
-------------
Comic and manga scans are often far bigger than the PSP needs. This makes
copies about a quarter of the size (needs 7-Zip installed):

    powershell -ExecutionPolicy Bypass -File Tools\shrink-books.ps1 -Source "My Books" -Dest "PSP Books"

It also converts .cbr (RAR) books, which ZOID can't read, to .cbz.
shrink-dropped.ps1 does the same for files dragged onto a shortcut to it.


FILES ZOID WRITES
-----------------
Settings, resume position, saved stations, book progress and bookmarks are
kept as text files next to EBOOT.PBP (zoid-settings.txt, radio.txt and so
on). Delete zoid-settings.txt to reset the settings.


KNOWN LIMITS
------------
- Apple Lossless (ALAC) .m4a files are not supported.
- .cbr (RAR) books must be converted to .cbz (see SMALLER BOOKS).
- Radio plays MP3 and AAC stations. HLS and Ogg/Opus stations are not listed.
- The iHeartRadio directory is the one iHeart's website uses. It is not an
  official public API and may change.


CREDITS
-------
BearSSL by Thomas Pornin (MIT license, see LICENSES/BearSSL.txt) for HTTPS.
dr_flac and dr_wav by David Reid (public domain or MIT-0) for FLAC and WAV.
Station directory: Radio Browser (radio-browser.info).
Lyrics: LRCLIB (lrclib.net). Podcast directory: Apple's iTunes Search API.
Built with the PSPDEV toolchain.
