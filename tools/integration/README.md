# masanork integration branch

`main` on masanork/MeOS follows shi3z/MeOS. `masanork` collects the development
features; use feature branches for focused upstream pull requests. Merge upstream
updates into `main`, then merge `main` into `masanork`. Do not reset a working
checkout to synchronize it: the original development checkout still has pending
files and two local commits beyond upstream.

## Imported feature tips

The initial integration starts from upstream `a6a9f234e509737f9fe84fe13468a8ec78b60fc9`
and the two existing Tab5 touch/hardware commits. Every tip below is an ancestor
of the integration commit. This table pins the imported revisions; subsequent
feature-branch updates need another merge.

| Branch | Imported revision |
| --- | --- |
| `pr/app-common` | `87189cff98230e94ee17d01f97b435483e99a975` |
| `pr/app-simulator` | `95b084c26b135d5418b72b8f905e4a952ce66f75` |
| `pr/basic` | `fbb8bab9e460af9827e622d170b5cf4719c698fe` |
| `pr/doom` | `a64d96bf1013d22b12f382fd2ee4317fc3758302` |
| `pr/kernel-foundations` | `ca8ada6eb8c8d159b45e1e6c9ef615ea5d38646e` |
| `pr/peripheral-reservations` | `7fa284013e937e0363471f0bb7ead4d204885ecb` |
| `pr/scheme-editor` | `912f8f8c32a26639a2994169792503337a1ffd73` |
| `pr/tab5-audio` | `bf93788541d94d9ce8abea21ee10a4f43e9cb9c3` |
| `pr/tab5-bitmap-resize` | `ee8fef770177d27c41ee0aa98179a4d7b35f3809` |
| `pr/tab5-hardware-diagnostics` | `5d82fc19e99a7b52d72826cb577c269fdd41fe9a` |
| `pr/tab5-i2c-recovery` | `0ba17604cb38ff56a6184685338fff37d65e8fb7` |
| `pr/tab5-microphone` | `97c4da646b9386cf40a9933f64629aaef3e5b7b3` |
| `pr/tab5-multitouch` | `09314e9790fbccf54f5e5fe34c0af7a8744ab142` |
| `pr/tab5-psram-bss` | `f1a7335da2e032fa3ac8ec4bdaae97b10eb7dcc5` |
| `pr/tab5-sd-write` | `8cf6338690514de0207461bc5240b53e1aedb18f` |
| `pr/tab5-st712x` | `b7e9da870cd063030987c3309aa4bd372ec1f2c7` |
| `pr/task-stack-diagnostics` | `0ddaadaa0238c68b01c47e20eca71bbcef7b0071` |
| `pr/vfat-mp3-player` | `89f53eff40ac578a8103a18037798a1794b626eb` |

## Shared changes and conflict resolutions

- Preserve the foundation layer's priority inheritance, retryable cleanup,
  microphone ownership checks, I2C timeout/error diagnostics, watchdog reservation,
  PSRAM bounds and selectable host compiler when merging earlier hardware branches.
- Combine BASIC, Scheme, writing editor and Doom commands, includes and build
  definitions rather than replacing one application's shell integration with another.
- Import the pending `/data/bas` and `/data/scm` hierarchy and custom directories,
  legacy record reading, updated storage fault tests and `data` command alias.
- Combine pending ST712x fast report reads with foundation I2C timeout diagnostics.
  The fast path restores normal timing under the bus lock, including on failure.
  Host tests cover fast ACK/NACK/timeout, slow fallback and subsequent normal timing.
- Keep the newer 767-byte Tab5 VFAT name bound, MP3 player, writable SD support,
  scheduler floating-point context, source export and CI fixes from the feature
  branches. The original dirty checkout has older copies of these files.
- Carry the remaining shell/WASM filename bounds checks and update storage
  diagnostics to reflect explicit SD mounts, preserving RTC/microphone diagnostics.

The original standalone `tools/sd/vfat_vfs_test.c` and its older fixture family
are superseded by the merged SD/player UTF-8, path-boundary and real-VFS
read/write/remount suites. The older original checkout is preserved for comparison.
`experiment/quake` remains separate: it is an experimental port, not an imported
`pr/*` feature. No default branch setting, upstream ref or original working file
is changed by this integration.

## Verification

Run from the integration checkout:

```sh
sh tools/host_checks.sh
sh tools/common/host_test.sh
sh tools/basic/host_test.sh
sh tools/sim/check-clean.sh
python3 tools/doom/release_test.py
sh tools/doom/host_test.sh
sh tools/build.sh tab5 build
M5OS_WATCHDOG=1 sh tools/build.sh tab5 reconfigure build
sh tools/doom/build.sh --without-wad build
```

Host tests and simulator checks use sanitizers and fresh source export where
supported. Firmware builds compile/link the combined tree, with Doom optional;
the WAD-free configuration includes BASIC, Scheme, writing editor and MP3 player.
Build output is local and ignored. Hardware flashing, interactive coexistence,
flash-store provisioning/reboot/power-cut, microphone and watchdog reset behavior
require separate device qualification. No board was flashed or reset during this
integration. Existing device evidence on individual features does not qualify
the newly combined revision.
