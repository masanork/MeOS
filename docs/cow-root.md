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
