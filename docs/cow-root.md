# Immutable system images and transactional block overlays

Status: experimental host-only C implementation. No firmware wiring, SD writes,
formatting, filesystem dependency, or boot change. Primary dirty checkout preserved.

## Storage contract

`fs/cow/cow.c` exposes a 512-byte sector view over an immutable base reader and
an append-only, preallocated zero-filled log. The base identity and sector count
are recorded in every checksummed record. The caller must derive a stable identity
from the exact base image (prefer a verified cryptographic image manifest); the
prototype's 64-bit identifier is an association check, not authentication.

Begin a transaction, write sectors, then commit. Reads during the transaction see
pending writes. All callers must be serialized, and M5FS may not expose partial
transactions to another task. Data records are synced before the commit record;
the commit record is then synced before success is returned. Recovery publishes
only complete consecutive generations whose transaction digest verifies. A sync
error has an uncertain outcome: recovery may find either the old or new generation.
The live writer is sealed on uncertain IO. A torn/uncommitted tail is never reused;
compact committed data into a fresh log before resuming writes.

Callbacks must implement exact reads/writes and durable, ordered sync. This is a
conditional guarantee, not proof that an SD card's internal FTL obeys it. CRCs
provide accidental corruption detection, not adversarial integrity. Arbitrary
media corruption can lose earlier generations; fail-safe replication is separate.

The reference format is experimental and is not an on-device ABI. It uses 552-byte
records, so it requires a byte-addressed file adapter, not direct aligned sector
writes. Each record includes type, base identity, generation, LBA, geometry, sector
payload and CRC. Commit records bind the ordered data-record CRCs in that generation.

## Memory and performance

No heap or capacity-sized index. Approximately one record of automatic scratch per
call, plus constant-size state. Reads scan the committed log (and the pending tail
inside a live transaction); this is intentionally slow and establishes a reference
for an indexed implementation. Do not ship it as the production read path.

No compaction, snapshots API, authentication, concurrency control, or erase handling
is implemented. Empty/full logs never trigger automatic formatting or overwrite.

## Integration sequence

1. Test actual M5FS operations against this view, with explicit transaction fences.
   M5FS currently flushes metadata on close/sync and performs some mutations earlier;
   simply committing every sector or wrapping close is insufficient. On failed
   transactions, discard/reload M5FS in-memory metadata before further operations.
   Add tests for create, grow, truncate, unlink and multiple open files.
2. Define `/system` RO separately from persistent user data. Keep user data across
   system image upgrades. Binding an old block overlay to a different base is forbidden.
3. Compare littlefs-backed files with a dedicated append-only block store. An outer
   filesystem is optional; adding it must earn its RAM, write-amplification and porting
   cost. Do not assume littlefs on SD inherits raw-flash guarantees without testing.
4. Implement durable storage adapter, explicit fresh-log initialization, compaction
   and atomic checkpoint selection. Keep the old checkpoint until the new reachable
   data is durable. Budget spare capacity and bound checkpoint/index RAM.
5. Wire boot: Flash startup/kernel -> SD storage -> verified base + committed overlay
   -> M5FS root. No card/root failure may automatically format media. Use a small
   rescue RAM root and skip the normal 8 MiB RAM disk on success.
6. Qualify real-card write ordering, power removal, restart recovery, latency, heap,
   stack and interaction with Wi-Fi. Card erasure is authorized by the user, but this
   phase performs no card access. Identification must precede any destructive tool.

## Evidence

Run `sh tools/cow/host_test.sh`. ASan/UBSan test 3,314 byte-cut scenarios across
all three records of a two-sector transaction, with immediately durable writes and
writes buffered until sync. Every recovered transaction is entirely old or new.
Tests also cover both sync failures, wrong base identity, uncommitted tail sealing,
CRC corruption, invalid LBA and exhausted capacity. This is synthetic storage
fault evidence; it is not M5FS integration, an outer-filesystem test, a device build,
or physical power-loss qualification.

## Real M5FS adapter and crash campaign

`fs/cow/blockdev.c` now presents the overlay as the existing `blockdev_t`. The
host harness mounts the real `fs/m5fs/m5fs.c`, explicitly begins a transaction,
performs file operations, flushes M5FS metadata/superblock with `fs_sync`, then
commits the overlay. Create, append/grow, truncate, delete, mkdir and two open
files pass 210 injected crashes (seven cut offsets per written record). Recovery
compares every virtual sector to the complete old/new reference image and mounts
M5FS again. The immutable base is checked unchanged. Separate processes discard
all volatile M5FS state, descriptors and heap after each simulated power loss.

This proves the externally fenced operation groups in the harness. Production
M5FS/VFS transaction APIs, concurrency exclusion, error recovery without reboot,
failed application-operation aborts and RO unmount semantics remain unfinished.
The adapter rejects writes outside a transaction. The existing M5FS unmount writes
a superblock even for a read-only session; the test explicitly expects that write
to fail while buffers are released. Firmware needs a proper discard/RO teardown.

## Tab5 memory baseline (2026-10-24)

Source and an already-existing device-session log were inspected; no serial port
was opened because a concurrent Quake session owned `/dev/cu.usbmodem101`.
The log's most recent heap sample reported KiB (despite its `KB` labels):

- Internal dynamic heap: total 192, used 112, free 79 KiB (integer truncation).
- PSRAM dynamic heap: total 27,431, used 8,869, free 18,562 KiB.
- RAM root backing: fixed 8,192 KiB, allocated in PSRAM irrespective of file usage.

Hardware PSRAM is 32 MiB. The current board reserves 4 MiB for IDF/display use;
external static BSS/alignment is also excluded from the main heap in the primary
checkout. Thus total dynamic heap varies by linked firmware. The latest inspected
sample is ~26.79 MiB total / 8.66 MiB used / 18.13 MiB free, not a fresh live query.
It comes from `/tmp/meos-quake-device.log`, which was still being updated during
inspection. Dynamic allocations and fragmentation depend on applications/history.

Moving the root backing to SD avoids an 8 MiB allocation, not an increase in physical
RAM or the heap arena size. Under the same workload, free PSRAM would approach
26.13 MiB before subtracting new storage overhead. A 128 MiB M5FS image increases
the resident allocation bitmap from 2 KiB to 32 KiB, reducing the gain by ~30 KiB;
the directory table stays 8 KiB. Outer-FS cache/index/compaction buffers still need
budgets. A realistic initial target is roughly 7.5–8 MiB net reclaimed, subject to
measurement; no achieved firmware memory reduction is claimed. Internal SRAM does
not gain 8 MiB, and current heap fragmentation may prevent one large allocation.

Flash already holds the bootloader and kernel. The proposed change is SD-backed
root/data, not copying the kernel from SD into RAM. The full 4 MiB display reserve
is not automatically reclaimable: separating reservation from actual display
allocation is independent tuning work. The 192 KiB heap is not total chip SRAM;
static kernel data, code/IRAM, startup and display/DMA allocations are separate.
