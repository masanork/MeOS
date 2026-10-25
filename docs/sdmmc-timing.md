# Native SDMMC stage timing

Wi-Fi's `bus_wait_us` includes both controller-lock contention and the delay
before the waiting task runs again. Native SD timings help distinguish DMA,
cache/copy work and scheduling effects before changing transfer sizing or lock
scope. The measurements are wall time, including preemption, rather than CPU
usage or a hardware-only transfer duration.

`sd stats` reports separate read/write buckets for transfers of up to 1, 8, 32
and 128 sectors. Larger requests are already split by the driver. Each bucket
contains calls, sectors, errors, cumulative milliseconds and peak microseconds:

- `wait`: before controller-lock acquisition to its return
- `prepare`: staging writes, cache/descriptor preparation, idle/FIFO waits and
  controller setup, up to command launch
- `dma`: command launch through observation of completion/error, including any
  time the polling task is preempted
- `finish`: DMA stop, status cleanup, write-busy wait or read-cache invalidation
  and copying into the caller's buffer
- `hold`: lock acquisition through transfer completion, before recording counters
  and unlocking

`max_polls` is a loop-iteration count, not elapsed microseconds. Peak stage
values may come from different requests and must not be added together.
Cumulative values are rounded down to milliseconds for console output. Counters
are bounded by their integer representation and should be reset between trials.

`sd stats reset` resets diagnostics only. Both commands are permitted while SD
is the active root and perform no card probe, media write or transfer. The SPI
transport reports these native diagnostics as unavailable. Existing root-owner
guards still prohibit raw probe, FAT mount/unmount and raw benchmarks.

## Paired SD/network workload

With an idle MeOS board and the existing PAK/font assets:

```sh
python3 tools/sdroot/wifi_coexistence_device.py /dev/cu.usbmodemXXXX \
  --reset --settle-seconds 60 --pps 100 --reads 1 \
  --diagnostics --sd-diagnostics \
  --log /tmp/meos-sd-stages.log --report /tmp/meos-sd-stages.json
```

The harness records native counters after each quiet CRC read and after each
network/graphics trial. Diagnostic resets/snapshots occur outside the traffic
phases. Complete read/write bucket receipts are required; missing or truncated
rows and any native transfer errors fail the measurement. Existing packet,
CRC, host-clock and memory checks remain active. On macOS, prefix the command
with `caffeinate -i` to prevent idle sleep only for that process's duration.
Reset discards unsaved RAM; raw transcripts may contain network identity.

## Validation boundary

Host root-owner checks cover diagnostic access/reset while mounted without
probing or mounting the card. Python tests reject incomplete/duplicate class
receipts. Full host tests, clean-source simulator replay and a Tab5 build pass.
The simulator does not emulate the controller or these physical timings.

Device timing data has not been collected: the attached board booted a different
OS, and the harness stopped before sending workloads. Port availability alone
does not establish firmware or partition-layout identity. Check both before a
future flash; the workload guard does not validate a flash operation's target.
The profiling image was written at the previously used MeOS app-slot address,
without changing bootloader, partition table or boot-selection metadata; its
effect on another OS's storage map has not been established.
