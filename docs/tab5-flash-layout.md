# Tab5 flash, SD assets and host-managed OTA

The default 16 MiB layout keeps a factory recovery image and adds two equal OTA
slots. Large, replaceable assets and the persistent root live on microSD.

| Range | Size | Purpose |
|---|---:|---|
| `0x000000..0x008fff` | 36 KiB | bootloader and partition table |
| `0x009000..0x00efff` | 24 KiB | NVS |
| `0x00f000..0x00ffff` | 4 KiB | PHY init |
| `0x010000..0x3effff` | 3.875 MiB | immutable `factory` recovery app |
| `0x3f0000..0x3fffff` | 64 KiB | existing raw SSH-key reservation; secure-storage redesign deferred |
| `0x400000..0x401fff` | 8 KiB | ESP-IDF `otadata` |
| `0x410000..0x7effff` | 3.875 MiB | `ota_0` |
| `0x7f0000..0xbcffff` | 3.875 MiB | `ota_1` |
| `0xbd0000..0xffffff` | 4.1875 MiB | unallocated |

The old `imedict` and `wasmapps` partitions no longer exist. The large IME
dictionary is `/assets/ime-dict.bin` on the outer littlefs SD volume. Persistent
WASM applications use `/apps` in the transactional SD root. A no-SD recovery
boot seeds only the firmware's built-in WASM samples into its volatile RAM root;
installs made there are intentionally lost on reset.

## First migration

Changing the partition table changes the meaning of flash from `0x400000`
onward. Before the first flash:

1. Take a full 16 MiB backup with
   `tools/tab5_ota.sh -p PORT backup backup.bin`.
2. If a large IME dictionary is wanted, upload it first with
   `tools/ime_flash_dict.sh -p PORT`. This copies it to SD, outside the changed
   internal-flash layout.
3. Build and flash the bootloader, partition table, initial empty `otadata` and
   factory app with `tools/build.sh tab5 flash -p PORT`.
4. Verify a factory boot, the persistent root, `/assets`, and the IME asset
   before installing either OTA slot.

The flash operation does not format or replace the microSD littlefs/M5FS store.
Keep the full-flash backup private if it contains a raw SSH key.

## A/B workflow

Build normally, then write an inactive slot and activate it as separate steps:

```sh
tools/build.sh tab5 build
tools/tab5_ota.sh -p PORT install 0
tools/tab5_ota.sh -p PORT activate 0
# reset, then verify boot and storage
tools/tab5_ota.sh -p PORT status
```

Use slot 1 for the next candidate. To return to the factory recovery image:

```sh
tools/tab5_ota.sh -p PORT factory
# reset
```

Activation is host-managed. ESP-IDF automatic rollback remains disabled because
MeOS does not yet mark a newly booted image valid after a health check. Enabling
rollback without that confirmation path would cause a healthy candidate to be
marked aborted on its next reset. Secure boot, signed manifests, anti-rollback,
key custody and the raw SSH-key reservation are separate secure-storage work.
