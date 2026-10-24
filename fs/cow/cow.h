#ifndef MEOS_COW_H
#define MEOS_COW_H
#include <stdint.h>
#include <stddef.h>
/* Experimental, caller-serialized, bounded-memory overlay. Offsets are bytes.
 * Callbacks must transfer exactly n bytes or return nonzero. sync must make
 * prior writes durable and ordered. Base identity must identify immutable bytes.
 * Log is preallocated and initially all zero; no automatic formatting. */
#define COW_SECTOR 512u
#define COW_RECORD 552u
struct cow_io {
    void *ctx;
    uint32_t capacity;
    int (*read)(void *, uint32_t, void *, size_t);
    int (*write)(void *, uint32_t, const void *, size_t);
    int (*sync)(void *);
};
struct cow {
    struct cow_io log;
    void *base_ctx;
    int (*base_read)(void *, uint32_t, void *);
    uint64_t base_id;
    uint32_t sectors, end, committed, generation, digest;
    int active, sealed;
};
int cow_mount(struct cow *, struct cow_io, uint64_t base_id, uint32_t sectors,
              int (*base_read)(void *, uint32_t, void *), void *base_ctx);
int cow_read(struct cow *, uint32_t lba, void *);
int cow_begin(struct cow *);
int cow_write(struct cow *, uint32_t lba, const void *);
int cow_commit(struct cow *);
#endif
