# Experimental Quake on MeOS / Tab5

Worktree: `experiment/quake`, based on the extracted Doom/application/hardware
PR stack. MeOS is shi3z's work; no root license is assigned here. This prototype
is local only. No PAK, game assets or firmware release is included.

## Current milestone

- The Quake 1 software-renderer engine and MeOS adapters link for ESP32-P4 using
  ESP-IDF 5.4.4 and the existing RISC-V toolchain.
- 320x200 indexed rendering expands to the 960x720 center viewport; rails carry
  basic touch keys. Keyboard W/S move, A/D turn, F fires, Space jumps,
  Enter selects, Escape opens the menu, Q/Ctrl-C stops the app.
- Quake uses a separate image, not a concurrent Doom/Quake configuration. The
  default MeOS build remains Quake-disabled.
- The engine has a 12 MiB PSRAM arena. Large renderer scratch arrays move to
  resettable external BSS, leaving a 48 KiB internal task-stack request.
  Availability and measured stack usage still require device evidence.
- Private read-only stdio routes through the MeOS VFS, including random seeks.
  `/sd` data is streamed rather than loading an entire PAK into RAM. Handles,
  owner allocations and display/input state are reclaimed through app cleanup.
- Audio, CD music, network drivers, recording, config/save writes and savegame
  loading are disabled. Only local loopback client/server is compiled.

## Build / checks

```
sh tools/quake/host_test.sh
sh tools/quake/build.sh
```

Outputs are `build-quake` and `sdkconfig.quake`, separate from Doom. The host
ASan/UBSan checks cover private file handles, read-only paths/modes, seek,
EOF/errors, handle exhaustion/cleanup, allocator and floating formatting.
The default checks do not run the full engine or exercise hardware.
Optional full-engine ASan smoke with separately supplied data:

```
python3 tools/quake/headless.py /path/to/pak0.pak --image /tmp/quake.ppm
```

The shareware v1.06 PAK (18,689,235 bytes, SHA256
`35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af`)
was tested locally through 130 software-renderer frames and a captured stage image.
This found and fixed a missing `getc` redirection in demo playback. Engine-marked
external-BSS variables are also folded into the resettable engine BSS so repeat
launches restore all state. The frame test is host evidence, not physical evidence. The engine retains legacy
parsers and assumes compatible trusted game data; this is not a hardened loader.

Local preparation on 2026-10-24: adapter/transfer tests, a complete real-engine
ASan host render (130 frames), the Tab5 firmware build, and a separate WAD-free
Doom regression build pass. The Quake image boots on the physical Tab5. A shareware
PAK was transferred to the original SDSC card with matching incoming-stream CRC
and a clean unmount. A FAT32 16 GB SDHC card was separately prepared through the
Mac reader with matching SHA256 and safe eject. Gameplay, native FPS and
quit/restart/forced termination remain to be qualified after the card swap.
Do not transfer historical Doom performance claims to this port.

## Next device check

Provide compatible Quake data separately, with `/id1/pak0.pak` on a FAT microSD
card (registered data may also have `pak1.pak`). Do not commit or redistribute PAK
files here. With the experimental firmware, use `sd mount ro`, then `quake`.
The new adapter always opens read-only, irrespective of the mount mode.
Back up the installed image before a coordinated device flash; another session
may be using the same device. First check PSRAM/stack, engine startup, first
frame, touch/keyboard input, quit, second launch and forced-stop cleanup. Measure
changed game frames separately from display presentations before optimization.

## Provenance

Sources are from Espressif's `esp32-quake` engine snapshot, pinned in
`third_party/quakegeneric/README.meos.md`. This in turn credits quake-generic and
id Software. Preserve GPL-2.0-or-later headers and LICENSE. Local engine changes
are documented there: dimensions and single-instance renderer scratch placement.
The Espressif FreeRTOS/BSP front end is not linked; MeOS supplies the runtime.

Primary reference: https://github.com/espressif/esp32-quake . That project reports
20–25 FPS at 512x300 on its P4 board; this is useful feasibility evidence, not a
Tab5/MeOS benchmark. Project-license and combined-work questions remain unresolved
before any binary release, as with the Doom experiment.

## Preparing a card

Use FAT32. Prefer copying `id1/pak0.pak` directly with a host card reader and
verifying its hash, then safely eject before inserting in Tab5. The 16 GB SDHC
card attached to the Mac was already FAT32; no format was performed.

An explicit developer transfer is available when a host card reader is absent:

```
python tools/quake/push_pak.py PORT /path/to/pak0.pak --prepare-sd --readonly-after
```

This uses `quake receive BYTES CRC32hex`, refuses an existing fixed destination,
and bounds size, times out, verifies received CRC and deletes a failed new file.
It writes only in this explicit preparation mode; the engine adapter stays read-only.
A serial link and synchronous 4 KiB VFS writes are substantially slower than a host
card reader. Default host tests cover overwrite refusal, two-block transfer,
CRC failure, timeout, write/close failure and cleanup. `quake verify` separately
reads the card back and reports size/CRC; receiver CRC alone verifies the incoming
stream rather than a fresh storage read. Use a known source CRC for comparison.

Press P during play to capture a diagnostic indexed engine frame to serial:

```
python3 tools/quake/frame_dump.py DEVICE_LOG /tmp/quake.png
```

The capture is the engine frame/palette, not a camera image of the physical LCD.
The profiling interval begins after Host_Init and reports presentations/second;
it does not claim every image contains changed scene content. Stack paint/guard
samples are diagnostics rather than exact maximum-depth proofs.
