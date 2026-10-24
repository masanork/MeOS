# COW storage core (review slice 1)

This change extracts the storage-only layer from `cd95cc15e6eea542550402c05a0d44bfd523e20f`.
Review base: `fb1accf831114e63f84baf382eb3379e63ddf568` on `review/fs-base`.
The current `masanork` branch already includes SD-root and Quake integration;
this stack exists to review the filesystem layers independently, not to roll back
or merge them into `masanork` again.

No M5FS changes, littlefs dependency, SD adapter, boot-path changes, format command,
partition changes or application changes are included. The blockdev adapter only
maps sectors; callers serialize and explicitly own transaction fences. Existing
boot paths never instantiate this backend.

## Durable block model

`fs/cow/cow.c` exposes 512-byte sectors over an immutable base reader and an
append-only preallocated log. Data and commit records have CRC32, base identity,
geometry and transaction generation; commit records bind the ordered data-record
CRCs. Data is synced before a commit record is issued, and that record is synced
before success is returned. Recovery exposes only complete committed generations.
An IO/sync failure can have an uncertain outcome: the caller receives failure,
but recovery may select the old OR new complete transaction. Never assume a failed
commit means a rollback. Live writes are sealed until authoritative recovery.

The caller must bind the exact immutable base to a stable identity (prefer a
verified cryptographic manifest). The 64-bit identity here checks association; it
is not authentication. CRCs detect accidental corruption, not hostile modification.
Arbitrary post-commit media corruption may lose previously acknowledged data.

Callbacks transfer exactly the requested length or return nonzero. `sync` must
make all prior writes durable and ordered. Failure must be reported, not silently
acknowledged. The SD controller/FTL must actually meet this contract; software
fault tests cannot establish that. Byte-addressed records are 552 bytes and need
a file adapter or explicit aligned storage layer before direct SD use.

## Two-bank collection and publication

`fs/cow/store.c` manages two independent banks. Each has a 512-byte header with
base identity, geometry, epoch and CRC, followed by its COW log. `open` never
formats. Only the explicitly destructive `cow_store_format` resets both banks.

Compaction, including recovery from an uncommitted/torn tail:

1. Keep the selected committed bank untouched.
2. Reset only the other bank, then sync that reset before reusing it.
3. Write its header with the next epoch.
4. Copy each sector that differs from the immutable base into a fresh transaction.
5. Sync the snapshot data, append its commit and sync again.
6. Select the new bank only after that commit completes. Retain the old bank until
   the next collection needs to reuse it.

At restart, a bank without its first committed snapshot is unpublished. Choose the
highest valid published epoch; reject equal epochs, incompatible identities and
unreadable bank IO. IO uncertainty faults the writer. Recovery rescans both banks;
if a pending tail remains, it compacts committed state before permitting writes.

Compaction requires spare space: each bank must fit the current changed-sector
snapshot PLUS the desired transaction headroom. `cow_store_reserve` configures the
minimum free record budget before beginning an operation (including its commit).
If a larger operation still exhausts capacity, abort/recover and report failure;
never rerun arbitrary application callbacks automatically. Oversized snapshots and
IO errors leave the old bank intact. An epoch never wraps.

The optional caller-owned index uses four bytes per logical sector, rebuilt from
validated committed records; without it, reads scan the log. Collection scans the
logical volume and retains/rebuilds an installed index. No image-sized snapshot
buffer is allocated. Larger volumes need an index/cache memory budget and measured
collection latency/write amplification. Two banks consume storage, not two
image-sized RAM buffers.


## Validation

`sh tools/cow/host_test.sh` runs sanitizer checks for 6,628 indexed/unindexed data
and commit byte cuts, 17,506 two-bank collection byte cuts, uncertainty/corruption,
generation and capacity bounds, plus blockdev error/transaction boundaries.
All media are memory-backed. No serial port, physical SD or flash is opened.
Durability still depends on the real adapter/controller honoring sync ordering;
CRC and base identity do not provide authenticity. The on-disk format remains
experimental. Physical power-cut, latency and write-amplification qualification
are separate from these checks.
