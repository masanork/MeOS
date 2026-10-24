Quakegeneric engine snapshot from https://github.com/espressif/esp32-quake at fe81f2840bc658bc4b009e77b1c6302572446e46.
Engine inherits GPL-2.0-or-later notices. No game assets included.
Local header change: configurable framebuffer dimensions, default 320x200.
The Espressif ESP-IDF/FreeRTOS front end is not imported. Upstream generic platform source files are retained for provenance but not selected for this build.
Local renderer change: edge/surface scratch arrays are static (single engine task), placed in external BSS instead of consuming a large internal task stack.
Large fixed work buffers in common.c (PAK directory), model/edge/span/sprite/world rendering, client precache/console, and datagram generation are static resettable external BSS. The port runs one engine task; these functions are not made reentrant. GCC stack-usage reports are checked during the component build (16 KiB per-function ceiling; call chains/recursion still require runtime guards).

Performance follow-up: `D_DrawZSpans` keeps its already-FP32 intermediate in FP32
while scaling by exact powers of two. `D_PolysetSetUpForLineScan` uses signed
integer floor division for integer endpoints, repairing negative remainders.
Both avoid software double operations on the P4 without fast-math. A million
reference comparisons pass, and the 130-frame host render remains byte-identical.
The engine uses scalar `-O3 -fno-tree-vectorize -fno-strict-aliasing`; stack limits
remain enforced. These changes do not assign a license to the surrounding MeOS.
Vector length/normalization also uses an FP32 square root for its FP32 input and
output. GCC/Clang builds request `__builtin_sqrtf`, with the original double call
as the portable fallback. ESP32-P4 disassembly confirms `fsqrt.s`. A million
finite input comparisons and the same 130-frame image comparison remain exact.
