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
- The engine has a 20 MiB PSRAM arena. Large renderer scratch arrays move to
  resettable external BSS, leaving a 64 KiB internal task-stack request.
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
They do not run the full engine or exercise hardware. The engine retains legacy
parsers and assumes compatible trusted game data; this is not a hardened loader.

Local preparation on 2026-10-24: adapter tests and the complete Tab5 firmware build
pass. No device was flashed, no engine frame has been observed, no FPS is measured,
and quit/restart/forced termination are not physically qualified. Do not transfer
historical Doom performance claims to this port.

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
