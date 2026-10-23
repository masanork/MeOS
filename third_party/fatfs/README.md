FatFs R0.16, ChaN. Source: https://elm-chan.org/fsw/ff/arc/ff16.zip
Archive SHA-256: `99f7dc1f7e095356e4a9e3dbe29959090d8b948afe2bbc5441e52fdf4b85449e`.

`ff.c`, `ff.h`, `diskio.h` are upstream source with line endings normalized.
The permissive redistribution terms and copyright notices are retained in the source headers.
`ffconf.h` changes only FF_FS_READONLY=1, FF_FS_TINY=1, FF_FS_NORTC=1 and FF_CODE_PAGE=437.
Long file names, exFAT, formatting and write APIs are disabled; short-name aliases are exposed.
MeOS serializes FatFs operations with its own mutex, not a FreeRTOS/ESP-IDF filesystem wrapper.
