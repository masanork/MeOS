# Tab5 audio output

`audio test` plays a 1.2-second, low-volume triangle tone. `audio` reports
DMA block and error counters. ES8388 gain starts at -18dB; the microphone ADC
is off. Output is 24kHz, 16-bit stereo on I2S0 and AHB GDMA TX2.

The driver uses a kernel worker and eight 512-frame buffers in internal SRAM.
Descriptors occupy separate configured cache lines (64 or 128 bytes); ownership is invalidated
before reading and flushed on return to DMA. PCM data is flushed too.
No FreeRTOS APIs are used. The worker retains its 4KB stack after playback;
PCM buffers are freed on close. The usual path allocates one contiguous 16 KiB
ring. If that fails, eight independent 2 KiB internal-SRAM allocations back the
same descriptor ring. Each block is cache-line aligned. Split allocation adds
up to seven alignment paddings and heap headers (1,008 bytes at 128-byte lines
with the current 16-byte-aligned heap); no permanent PCM reservation is added.
If any block fails, all partial allocations are returned before open fails.
A diagnostic reports free bytes and the largest free block sampled before
allocation when the contiguous path could not be used. This handles fragmentation
only when sufficient total internal memory and eight suitable holes remain. The first caller needs APP_PERM_SPAWN.

A task mutex serializes rendering and close. Callbacks must not block or call
`audio_close`. Application owners must pass their actual app ID; app teardown
stops audio before reclaiming owner memory. Close mutes the codec while clocks
are running, then stops DMA and frees buffers. Concurrent opens are rejected.
The codec and amp use fixed system-I2C helpers so accessory bus selection does
not redirect audio writes. One mutex covers each masked read-modify-write.

This PR excludes Doom integration and synthesis. Host I2C regressions cover
fixed-bus writes, atomic masked updates, routing and NACK handling. The actual
I2S/DMA/cache path and lifecycle changes require physical-device verification.

`sh tools/audio/host_test.sh` runs ASan/UBSan tests with the real MeOS heap at
64/128-byte alignment: a fragmented 192 KiB arena, nonoverlapping blocks,
neighbor canaries, every partial failure, cleanup, and repeated reopen. These
checks do not qualify I2S/DMA/cache on hardware or prove the observed device
failure was fragmentation rather than insufficient total SRAM.
