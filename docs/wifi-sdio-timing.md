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
task resumes; this change does not split blocked time from runnable time.

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

`--bus-diagnostics` requires `--diagnostics` and complete CMD53 read/write and
CMD52 rows. It rejects missing/duplicate rows, duplicate fields, incomplete
fields, scheduled peaks larger than hold, and inconsistent paired stage sums.
Bus command errors fail the workload; post-idle timeout observations are retained
for diagnosis. Existing CRC, ICMP integrity, loss, clock and memory limits apply.
Snapshots remain outside timed traffic phases. New paths must not already exist;
serial opening/reset discards unsaved RAM and transcripts may contain network
identity. Check device identity, partition layout and serial ownership first.

Nine offline tests pass, including receipt integrity checks. The full host suite,
clean-source application simulator and Tab5 build also pass. The simulator does
not exercise SDIO/controller timing. Physical timing has not been collected:
the initial read-only preflight could not acquire the serial port exclusively,
and no flash or workload was issued. A hardware handoff is pending. This adds
diagnostics; it does not establish a latency improvement or change lock behavior.
