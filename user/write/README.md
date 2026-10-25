# Writing editor

Tab5 shell: `write [FILE.txt]` (default `note.txt` in the current directory).
Simulator: `sh tools/sim/run.sh --write --workspace DIR --source /home/note.txt`.
Create DIR first; host files are confined to this workspace. F12 captures a BMP
when `--screenshots DIR` is provided.

BIZ UD Gothic Regular is rendered at 24 native pixels on a warm light page.
The only editor chrome is a single line at the top containing the writing mode,
modified mark, essential shortcuts, IME state, transient status and path (in
that order, so a long path is clipped first). The document begins immediately
below it and extends almost to the bottom edge. A vertical column fits 21 normal
full-width characters; horizontal mode displays 18 lines.
Write opens in Japanese vertical prose mode: text flows down from the upper
right and subsequent columns advance left, with 6px character tracking and a
40px column pitch. BIZ UD's OpenType `vrt2` alternates and vertical metrics place
punctuation, brackets, long vowels, small kana and rotated Latin correctly.
Closing marks may hang into the lower margin, while opening and closing brackets
receive a bounded Japanese line-start/line-end prohibition pass. This is not a
full JLREQ layout engine.

Ctrl-T switches immediately to the original proportional horizontal layout with
36px line pitch. In vertical mode Up/Down moves within a column and Left/Right
moves between columns; Home/End select its top/bottom. MeOS IME preedit is
inserted at the document cursor in either direction, reflowing following text
without entering the file. Its two-column candidate popup remains horizontal
beside the active column. Ctrl-Space toggles the IME; Space converts and Enter
commits. Ctrl-S saves, Ctrl-Q exits (repeat to discard unsaved edits), and Ctrl-Z
undoes the last edit. PgUp/PgDn navigate a screenful. Buffer limit 16 KiB, one
undo snapshot. UTF-8 files and MeOS VFS are used.

Only BIZ UD is used. The Tab5 loads `/assets/fonts/bizud24.m5f` from the
persistent SD root into PSRAM while this editor is active; the simulator uses
the byte-identical generated atlas directly. Missing glyphs show a box.
Combining marks and IVS are not shaped as sequences yet. The atlas contains
11,741 ordinary cmap glyphs and 350 Unicode-addressable `vrt2` alternates and is
about 2.35 MiB. Its vertical index is a CRC-covered version-1 extension, so an
older OTA image can still load and use the ordinary bank;
a full offscreen RGB565 frame (about 1.76 MiB) is also allocated in PSRAM to
avoid clear/redraw flicker. The framebuffer is released on exit; the shared
BIZ UD atlas remains warm for Scheme. `mem trim` or opening a non-editor from
Home returns that idle cache to PSRAM. The same BIZ UD IME presentation is
shared with the Scheme workspace.

`sh tools/sim/write_test.sh` replays the real MeOS IME through HID input, saves
Japanese text, verifies exact UTF-8 file contents, captures vertical composition,
and verifies that Ctrl-T produces a distinct horizontal frame.
