# Tab5 Quake physical qualification — 2026-10-24

MeOS remains shi3z's project. This records a local prototype, not a release or a
new project license. Game data and captured game images are not committed.

## Device and data

- ESP32-P4 Tab5, ST7121 display, ST712x touch, 360 MHz, ESP-IDF 5.4.4.
- 16 GB SDHC, 30,867,456 sectors / 15,072 MiB, FAT mounted **read-only**.
- Shareware v1.06 PAK0: 18,689,235 bytes. Fresh SD read returned CRC32 `74e3b084`,
  matching the host source. SHA256 is recorded in README.md. No format performed.
- Original application partition was backed up separately before experimentation.
  No bootloader, partition table, dictionary or application-store flash region was
  replaced. Local binary/symbol backups remain outside the repository.

## Passed

1. Full engine linked/booted with 12 MiB arena and 48 KiB internal task stack.
2. Demo1/e1m3 rendered on the device; an indexed frame/palette capture was decoded
   and visually inspected. This capture is not a photograph of the physical LCD.
3. Serial Escape/Enter selected Single Player / New Game. `maps/start.bsp` loaded,
   local client/server connected, and the player entered the Introduction map.
4. Forward input moved the viewpoint toward the hallway. Fire input reduced
   ammunition from 25 to 24; before/after device engine-frame captures confirmed
   movement and HUD changes. Audio is intentionally disabled.
5. Three normal launches and quits without resetting the board. Each completed
   app reported 13,517,504 bytes reclaimed; no live app tasks remained.
6. After each quit, coarse used memory returned to 112 KiB internal and 8,869 KiB
   PSRAM. Free PSRAM was 18,562 KiB. Largest contiguous free block varied (13,138
   then 12,621 KiB); this is not a fragmentation-free or long-campaign claim.
7. SD unmount succeeded after gameplay, followed by read-only mount and restart.
8. Samples reported 15,344 / 49,152 stack bytes used, guard OK. Other task guards
   remained OK after quitting; no allocation failures were reported.

Post-load 100-presentation intervals ranged roughly **11.8–17.4 presentations/s**
across the tested demo/Introduction scenes. Loading intervals (2.3 / 3.4) and
capture stalls are not steady-state results. These counts are display submissions,
not a guarantee of distinct pixel contents, a full-game benchmark or 30 FPS.

## Bring-up fixes and residual checks

The first image tripped the scheduler's saved-SP check. Additional diagnostics
identified the incoming Quake task, rather than the outgoing task named in the
panic record. The PAK parser's 128 KiB automatic directory array and other large
fixed buffers were moved into resettable PSRAM BSS for this single-engine task.
Host ASan rendering still completed 130 frames, with byte-identical captured PPM
before/after the storage-class changes. Engine GCC stack reports now top out at
9,792 bytes and are checked against a 16 KiB per-function ceiling. Runtime stack
checking is kept enabled; this static ceiling does not bound recursive call chains.

Physical touch hit targets/menu selection, long-play/other levels, forced task
termination, camera verification of LCD output and audio remain separate checks.
The final FIRE/OK context mapping is a follow-up to the serial Enter qualification;
it was also exercised through serial F to enter New Game, but physical finger
interaction must not be inferred from that test.
