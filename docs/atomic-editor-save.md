# Whole-buffer editor saves on the transactional SD root

BASIC SAVE, Scheme save-buffer, the Japanese writing editor and the regular text
editor now call `vfs_replace_file`. This includes the text editor launched by BASIC
EDIT. Buffers are borrowed during the call; no extra full-file copy is allocated.
The modified/saved flags change only after the whole operation returns success.

## Storage contract

For transactional M5FS, truncate/create, data writes, close, metadata flush and COW
commit run under one volume mutex. A successful call publishes exactly one
transaction. A write/space/metadata error before commit abandons the new version
and remounts committed state; an existing file retains its old contents and a new
file stays absent. The conservative metadata/data record hint permits collection
before starting a large save rather than running out of log space midway through it.

An error at the final durability barrier has an uncertain outcome: recovery can
select the old file OR the complete new file. The application still reports failure
and retains its buffer. There is no unsafe retry through ordinary open/write after
an atomic-handler error. Guarantees depend on the existing storage sync contract;
physical SD power-loss tolerance is not established by this change.

Any recovery invalidates all open M5FS file handles, as with the existing transaction
API; affected callers must reopen. This is durable whole-file replacement, not a
snapshot guarantee across multiple independent reads on an already-open descriptor.

Raw RAM M5FS volumes keep their prior flush behavior. VFAT, the host simulator and
other filesystems without a replace hook use the shared streaming writer; those
paths preserve prior short-write and close-error handling and do not acquire a new
atomicity guarantee. The existing `/data` store retains its own close/commit model.

## VFS integration

The optional provider `replace` callback is selected after path normalization and
normal mount matching. M5FS enforces filesystem-write permission before touching
the volume; write-only callers do not acquire an additional read requirement.
Callback errors, including ERR_NOSYS, are final. Only an absent callback selects
legacy streaming behavior. This prevents permission or IO failures being retried
through a destructive truncate.

A transient descriptor pins the mount during a provider callback. Interrupts are
restored before IO. Ordinary descriptor operations reject the transient pin, and
application cleanup can reclaim it after its owner terminates. Legacy streaming
saves use only their normal open descriptor, including when the table is nearly
full. The pending flag uses existing structure padding on the target.

`written` is optional. Atomic failures report no acknowledged bytes; legacy failures
may report partial writes. Success always includes close/commit completion.

## Verification

- Real VFS/M5FS tests cover one commit per save, write permission, read-only mounts,
  old-file rollback, new-file absence on failure, zero-byte replacement, short
  writes, both sync-error outcomes, descriptor cleanup and continued saves.
- The real transaction/replacement crash harness covers 987 sampled byte cuts,
  including 16 KiB and 32 KiB replacements and empty/new files. Every recovered
  sector matches the complete old or new image; recovery mounts the volume again.
- The existing 6,628 record cuts and 17,506 collection cuts remain in the COW suite.
- VFS dispatch tests cover mount pinning, owner termination cleanup, no retry after
  callback errors, partial streaming writes, close errors and the last free slot.
- BASIC and regular editor host error cases retain source state. Scheme IDE failure
  cases retain modified text. The clean-source SDL replay exercises Japanese saves
  and six repeated Scheme/writing-editor launches through the new shared interface.

Commands: `sh tools/host_checks.sh`, `sh tools/basic/host_test.sh`,
`sh tools/scheme/host_test.sh`, `sh tools/sim/check-clean.sh`.

The simulator exercises the host streaming adapter; the real VFS/M5FS fault tests
establish the transactional path. These evidence categories are intentionally
separate from physical keyboard/display interaction or actual power removal.
