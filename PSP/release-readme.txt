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
    VIDEO/    .mp4 videos (convert them with Convert for PSP, see COMPANION TOOLS)
    BOOKS/    .cbz files, or folders of .jpg/.png pages. A folder of
              volumes is shown as a series.

Internet radio, podcasts and online lyrics use the Wi-Fi networks saved in
the PSP's own Settings > Network Settings. The WLAN switch must be on.


NAMES MUST BE PLAIN ENGLISH LETTERS
-----------------------------------
A real PSP cannot open a file or folder whose name has letters such as
é, ü, ½ or Japanese characters. Song tags can use any language; only the file
and folder names are affected. After copying files to the Memory Stick,
double-click Tools\Fix PSP Names.bat with the PSP connected (see COMPANION
TOOLS below).


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


COMPANION TOOLS (WINDOWS 10 AND 11)
-----------------------------------
The Tools folder has helpers for getting your files ready. Keep the Tools
folder together (the .bat files run the scripts in Tools\scripts).

  Add Desktop Shortcuts.bat   Run once: puts the four tools below on your
                              Desktop, so you can drag files onto them.

  Convert for PSP.bat         Drag videos (or a folder of them) onto it.
                              PSP-ready .mp4 files (480x272, with the
                              soundtrack and subtitles inside) appear in
                              "PSP Videos" on your Desktop. Copy them into
                              VIDEO. Works with .mkv .mp4 .m4v .avi .mov .wmv
                              .webm. Only DRM-free videos can be converted.

  Shrink Books for PSP.bat    Drag books (.cbz, .cbr, or a folder of them)
                              onto it. Copies about a quarter of the size
                              appear in "PSP Books" on your Desktop; .cbr
                              books, which ZOID can't read, become .cbz.
                              Drag the Memory Stick's BOOKS folder onto it to
                              shrink the books already there (it asks first).

  Fix PSP Names.bat           Double-click with the PSP connected. Lists the
                              file and folder names a PSP can't open, then
                              renames them after you press Y, keeping your
                              bookmarks and playlists working.

  Check Video for PSP.bat     Drag an .mp4 onto it to see whether ZOID can
                              play it as it is.

  Install Helpers.bat         Offers to install two free programs through
                              Windows' winget: 7-Zip (needed only for .cbr
                              books) and FFmpeg (optional; makes converted
                              videos smaller and sharper).

  PSP (ZOID) HandBrake presets.json
                              If you'd rather use HandBrake: Presets >
                              Import, then pick "PSP (ZOID) - English" or
                              "PSP (ZOID) - Japanese". Convert for PSP gives
                              more reliable sound on a real PSP.


FILES ZOID WRITES
-----------------
Settings, resume position, saved stations, book progress and bookmarks are
kept as text files next to EBOOT.PBP (zoid-settings.txt, radio.txt and so
on). Delete zoid-settings.txt to reset the settings.


KNOWN LIMITS
------------
- Apple Lossless (ALAC) .m4a files are not supported.
- .cbr (RAR) books must be converted to .cbz (Shrink Books for PSP does it, see COMPANION TOOLS).
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
