#pragma once
#include <m5os/ime.h>
#include <m5os/types.h>

typedef struct {
    int x;
    int baseline;
} bizime_point;

/* Return the insertion point after the preedit, including proportional wrap,
 * without drawing. Editors use this to reflow committed text behind it. */
bizime_point bizime_measure(const ime_view *view, int x, int baseline,
                            int left, int right, int pitch);

/* Draw a proportional BIZ UD preedit at the document insertion point and a
 * bounded two-column candidate popup close to it. The caller owns the
 * framebuffer and repaints the underlying document on every revision. */
bizime_point bizime_draw(const ime_view *view, int x, int baseline,
                         int left, int right, int top, int bottom, int pitch,
                         uint16_t fg, uint16_t bg, uint16_t accent);
