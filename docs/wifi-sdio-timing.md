# Wi-Fi SDIO lock occupancy

The [aligned-sector cache change](sd-sector-cache.md) reduced PAK CRC time but
did not reduce Wi-Fi's maximum controller-lock wait. Native SD lock occupancy
peaked around 14–22 ms in the paired loaded runs, while Wi-Fi wait peaked at
60–88 ms. The Wi-Fi transport also owns the shared controller and may itself
hold it during polling, cleanup or preemption. The existing wait counter alone
cannot identify which owner or stage caused the delay.

`wifi stats [reset]` now reports two `WIFI SDIO READ/WRITE` rows for CMD53 and
one `WIFI CMD52` row for register commands. Reset still changes diagnostics
only, preserving protocol state and incomplete RX fragments. Formatting uses
bounded 512-byte rows, rather than the shell's normal 160-byte output buffer.

CMD53 rows report call/error counts, pre/post idle-wait timeout counts,
cumulative controller-lock wait, preparation, DMA, cleanup, lock occupancy and
scheduled time. Cumulative times are truncated to milliseconds; peaks are
microseconds, saturating at `UINT32_MAX`. Counters should be reset between trials.

- Preparation includes the existing pre-transfer idle wait, copy/cache work,
  descriptor setup and FIFO reset, ending immediately before command launch.
- DMA spans command launch to observation of completion/error.
- Finish includes DMA stop, status cleanup, the existing 50,000 us idle-wait
  loop, read cache invalidation and copying, plus timing-snapshot overhead.
- Hold ends before recording counters and unlocking, as in the native SD stats.
- Scheduled time uses the current task's existing scheduler accounting. It
  excludes time running other tasks, but includes IRQ/kernel work attributed to
  this task. It is not a CPU cycle count or hardware-only DMA time.

`peak_prepare_us`, `peak_dma_us`, `peak_finish_us`, `peak_scheduled_us` and
`peak_bytes` all belong to the **same transfer** that produced `max_hold_us`.
Unlike independent stage maxima, the three wall-time peak stages add up to
that hold interval (unless saturated). Subtracting the paired scheduled time
estimates time when another task ran during that lock occupancy; it does not
separate ISR time from transfer polling.

CMD52 reports call/error counts, cumulative hold/scheduled time and a paired
maximum hold/scheduled sample. The existing `WIFI TIME bus_wait_us` remains
CMD53's wall time around lock acquisition, including delays before the waiting
task resumes.

### Ownership handoff and task resumption

`WIFI WAIT READ/WRITE` rows split each direction's maximum CMD53 wait into:

- `blocked_us`: timed mutex entry through the direct ownership grant, including
  the small amount of contention/donation bookkeeping before blocking;
- `ready_us`: ownership grant through return from the blocking scheduler call,
  before interrupts are restored; the recipient may own the controller while
  it is still not running;
- `other_us`: the residual outer wait, including call overhead and any delay
  before mutex entry or after the timed helper resumes.

These fields belong to the **same wait** as that direction's `max_wait_us` and
sum to it unless saturated. They are separate from the maximum-hold sample.
`owner_pid` identifies the owner at initial contention, not all intervening
owners. `contended` and `profiled` distinguish a real blocking wait from the
idle/no-scheduler bypass. `sd stats` exposes the same breakdown for each native
SD size class as `wait_blocked_us`, `wait_ready_us`, `wait_other_us` and the
corresponding owner/flags. Both sides must be measured: a slow-to-resume SD
recipient can block Wi-Fi before SD's transfer-hold timer even starts.

The optional `mutex_lock_timed` records entry, grant and resumption under IRQ
masking. It retains an integer timestamp in the task control block, not a pointer
to the caller's stack. This adds eight bytes per task (128 bytes for 16 tasks).
Ordinary mutex calls do not read the clock; the idle task keeps its existing
polling behavior and reports no timed profile. Creation and cancellation clear
the timestamp marker. Priority donation, direct handoff, waiter ordering and
the existing unlock/yield policy are unchanged.

The pre-transfer 200,000 us and post-transfer 50,000 us polling limits and all
register operations are unchanged. The post-transfer wait was previously
unchecked; its false return is now counted without changing the R5/DMA transfer
result. A post-idle timeout is therefore an observation, not an additional
transport error. Polling limits are requested delay-loop budgets; preemption
and register overhead can make wall time longer.

## Reproduction and validation

```sh
caffeinate -i python3 tools/sdroot/wifi_coexistence_device.py /dev/cu.usbmodemXXXX \
  --reset --settle-seconds 60 --pps 25 100 --reads 1 \
  --diagnostics --sd-diagnostics --bus-diagnostics \
  --log /tmp/meos-wifi-sdio.log --report /tmp/meos-wifi-sdio.json
```

`--bus-diagnostics` requires `--diagnostics` and complete CMD53 read/write,
WAIT read/write and CMD52 rows. With `--sd-diagnostics` it also requires native
SD wait breakdowns. It rejects missing/duplicate rows, duplicate fields, incomplete
fields, scheduled peaks larger than hold, inconsistent paired stage sums and
inconsistent wait parts/flags.
Bus command errors fail the workload; post-idle timeout observations are retained
for diagnosis. Existing CRC, ICMP integrity, loss, clock and memory limits apply.
Snapshots remain outside timed traffic phases. New paths must not already exist;
serial opening/reset discards unsaved RAM and transcripts may contain network
identity. Check device identity, partition layout and serial ownership first.

Nine offline tests pass, including receipt integrity checks. The mutex host tests
model a 10 us contention interval followed by 60 us of granted-but-not-running
time, zero-time/uncontended acquisition, killed owners/waiters and idle bypass.
They also assert that ordinary mutex calls never read the clock and profiled
reads occur with IRQs masked. The full host suite, clean-source application
simulator and Tab5 build pass. The simulator does not exercise controller timing.
This adds diagnostics; instrumentation overhead has not been calibrated and no
latency improvement or lock-policy change is claimed.

## Physical observations, 2026-10-26

[The partial device receipt](../tools/wifi/sdio-timing-device-20261026.json)
retains complete reports and private-log hashes for three passing runs. The
first firmware, `6f3e60e`, measures transfer stages. Firmware `00f5469` adds the
Wi-Fi mutex wait breakdown; it ran both uniform 25/100 pps and 12-packet bursts
at requested 100 pps. Each process waited 60 seconds with serial held open.
Both used 360 MHz CPU, 200 MHz PSRAM and native four-bit SD at 20 MHz.

No pre/post idle-wait timeouts or SDIO command errors occurred. CMD52 counts
were zero after reset, so these workloads do not qualify CMD52 performance.
In the first uniform run, CMD53 read hold peaked at 0.677 ms at 25 pps, despite
a 70.986 ms acquisition wait. At 100 pps, an 11.132 ms read hold included only
0.375 ms of scheduled time; an 11.019 ms write hold included 0.272 ms scheduled.
Thus those long hold intervals mostly involved running other tasks, not active
DMA polling. The unchecked post-transfer idle wait was not the observed source
of the long delays in these samples.

The later `00f5469` runs measured these **paired maximum-wait** samples:

| Traffic | CMD53 direction | Outer wait, ms | Before grant, ms | Grant to resume, ms | Other, ms |
| --- | --- | ---: | ---: | ---: | ---: |
| 25 pps uniform | Read | 20.279 | 9.664 | 10.608 | 0.007 |
| 25 pps uniform | Write | 68.596 | 68.407 | 0.186 | 0.003 |
| 100 pps uniform | Read | 69.566 | 68.981 | 0.575 | 0.010 |
| 100 pps uniform | Write | 18.735 | 10.479 | 8.253 | 0.003 |
| 100 pps, bursts of 12 | Read | 83.583 | 82.428 | 1.152 | 0.003 |
| 100 pps, bursts of 12 | Write | 20.270 | 11.737 | 8.523 | 0.010 |

Initial contention owner PID 8 was the shell in all six samples. The largest
waits were predominantly before grant, not after Wi-Fi received ownership.
At the same time, native SD's transfer-hold peaks in the uniform trials were
20.519/21.009 ms. This exposes a gap in the driver-level hold measurements:
ownership may already be reserved while a recipient is not running, and the
hold counters exclude recording/unlock edges. It does not establish which of
those intervals explains the full shell-owned delay. The final native-SD wait
instrumentation is intended to resolve this remaining attribution.

All **eight** PAK CRC reads matched `74e3b084` for 18,689,235 bytes. Native SD
and Wi-Fi transport errors, invalid ICMP replies and device reply failures were
zero. All loaded PAK/font/Write phases had zero loss. The 25 pps idle phases
received 68/69 and 66/69 replies (1.449% and 4.348% loss), within the existing
5% limit; the idle RTT spikes are retained and not assigned to the controller.
The later burst run still had font/Write RTT p95 of 451/378 ms. These results
do not show a latency improvement. All three clock checks passed; trimming
returned from 23,135 to 23,119 KiB free PSRAM within the unchanged 32 KiB limit.

### Final variant and hardware handoff remain incomplete

Firmware `f8cd1b7` adds the same wait measurements to native SD. It builds and
passes host adapter/parser tests; its 1,777,056-byte image has SHA-256
`d8713e1ac07fa0ff5dabde6ebd19e229c438ace37c2f4b678f451df8a90a02bc`.
The image was written and hash-verified in measurement slot OTA1. During the
first attempt, USB serial disappeared before activation; activation failed
before reading the partition table, and the subsequent harness could not open
serial. That interruption remains in the receipt's history.

Fresh preflight had identified original MeOS `333a366` in OTA0 at `0x410000`,
with the expected A/B map and active native SD root. OTA0, factory, bootloader
and partition table were preserved. Original OTA0 was selected before the final
OTA1 install. Only OTA1 and selection metadata were modified. No SD format or
asset mutation command was issued.

After USB reconnection, a fresh boot verified original `333a366` on OTA0 and
active SD root. `verify_flash` matched the stored OTA1 image to the final binary,
then activation succeeded and `f8cd1b7` booted on OTA1. The dictionary loaded
5,354,161 bytes with its stack guard OK. The final uniform harness nevertheless
stopped **before any workload**: the saved AP was unavailable (disconnect
reason 201, `NO_AP_FOUND`). A scan found four other networks and no saved AP;
no SSID or credentials were changed. The failed prerequisite report is retained
separately from the earlier passing measurements.

An offline SD/graphics attempt was then prepared, but USB serial disappeared
again before it could open the port; it generated no device measurements.
**The last successful selection is now measurement OTA1.** Original OTA0 is
preserved, but reselecting it and confirming its boot remain pending USB
reconnection. Final paired native-SD/network measurements also need an available
2.4 GHz AP reachable from the test host. The PR remains a draft; neither an
unstarted workload nor the missing restoration check is presented as passing.
