Quakegeneric engine snapshot from https://github.com/espressif/esp32-quake at fe81f2840bc658bc4b009e77b1c6302572446e46.
Engine inherits GPL-2.0-or-later notices. No game assets included.
Local header change: configurable framebuffer dimensions, default 320x200.
The Espressif ESP-IDF/FreeRTOS front end is not imported. Upstream generic platform source files are retained for provenance but not selected for this build.
Local renderer change: edge/surface scratch arrays are static (single engine task), placed in external BSS instead of consuming a large internal task stack.
