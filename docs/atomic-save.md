# Whole-buffer VFS saves (review slice 4)

Depends on [SD-root slice #3](https://github.com/masanork/MeOS/pull/3), stacked on
M5FS/COW slices #2 and #1. This review branch extracts the implementation from
`d94c7d8c9b114e7e279a584149774e4c48d7d8e7`, including its abandoned-owner pin fix.
The live `masanork` branch already contains that implementation; this is an
independent review slice, not a duplicate merge into it. Added tests and updated
API/user documentation are local to this slice. No device or SD card is accessed.

## Guarantees and fallback

`vfs_replace_file(path, data, len, written)` dispatches one complete buffer to an
optional filesystem `replace` hook. The M5FS adapter checks write permission and
uses `fs_replace_file` to hold the volume lock through truncate/write/close and
one transaction. The caller buffer is not copied into an image-sized staging
allocation. SD-root provides a no-IO record-budget hint; the subsequent begin can
collect before truncation when log space is low.

Success includes close/commit. With a transactional backend, recovery selects the
old OR complete new contents after uncertain I/O, never a partially saved file.
An error is not proof of rollback, so callers retain their modified buffers and
report failure. Atomic failures report zero confirmed bytes. Recovery invalidates
existing M5FS descriptors; callers must reopen them. Failed recovery stays offline.

A backend hook error, including ERR_NOSYS, is final: never retry via open/write
and truncate the previous file a second time. Backends without a hook use the
shared `vfs_write_file` streaming helper to preserve existing behavior. Raw M5FS,
RAM, FAT and Flash program-store behavior is not silently upgraded by this API;
legacy errors can leave partial content and report partial bytes. In particular,
the SDL host adapter uses this streaming helper rather than emulating SD commits.

## Ownership and applications

A transient app-owned descriptor pins the mount during a backend callback, with
interrupts enabled for IO. Normal fd operations reject the pending pin. The app
reaper can release it after terminating the owner's tasks, without calling close
on a nonexistent backend handle. No callback can continue after its owner has
been terminated/reaped; cleanup is not an API for cancelling a live callback.

BASIC SAVE, the BASIC text-editor handoff, shell edit, Scheme and the BIZ UD
writing editor use the shared replacement API. Modified buffers are cleared only
after successful replacement. Other streaming endpoints (shell echo/put, WASM
cache updates, arbitrary app writes) are not converted by this slice. Neither
source paths nor Flash partitions, root selection or game engines change.

## Verification

- `sh tools/atomic-save/host_test.sh`: VFS mount pin/unmount exclusion, owner
  cleanup, full fd tables, no fallback after hook errors, legacy short writes and
  close errors; real BASIC SAVE -> VFS -> M5FS/COW verifies source retention,
  write/permission failure, uncertain commit, remount, LOAD and RUN.
- `sh tools/cow/host_test.sh`: retains 6,628 core and 17,506 collection byte cuts,
  expands real-FS process crashes to 987 cases including whole-file VFS saves,
  and checks every recovered sector against complete old/new images. Grouped
  save tests cover permissions, one commit, empty file, disk full, reservation
  before truncate, abandoned owners, reader exclusion, offline recovery and heap
  cleanup. The earlier blockdev range tests remain in the review stack.
- `sh tools/basic/host_test.sh` and `sh tools/sim/check-clean.sh`: existing app
  errors/cleanup, Japanese saved content, slow-evaluation deterministic replay and
  repeated Scheme/writing-editor handoff. Source exports include the new helper.
- Foundation CI runs the new atomic-save suite along with SD-root/COW regressions;
  normal/watchdog/Doom builds verify firmware integration separately.

All storage here is memory-backed, and owner termination is injected at callback
boundaries. Real arbitrary-instruction termination, scheduler/IRQ behavior, SD/FTL
ordering and abrupt physical power loss require hardware qualification. Existing
source-branch device receipts do not qualify this exact extracted revision.
