#pragma once
#include <m5os/kmalloc.h>

/* DMA descriptors may point at independent blocks. Keep the usual single
 * allocation, but tolerate a fragmented internal heap without shrinking the
 * ring, using PSRAM, or reserving memory when audio is idle. Caller serializes
 * open/close and must stop DMA before release. */
#define AUDIO_PCM_BLOCKS 8u
#define AUDIO_PCM_FRAMES 512u
#define AUDIO_PCM_BYTES (AUDIO_PCM_FRAMES * 2u * sizeof(int16_t))
_Static_assert(AUDIO_ALIGN && !(AUDIO_ALIGN & (AUDIO_ALIGN - 1)), "PCM alignment is a power of two");
_Static_assert(AUDIO_PCM_BYTES % AUDIO_ALIGN == 0, "PCM blocks span cache lines");
typedef struct {
    void *raw[AUDIO_PCM_BLOCKS];
    int16_t *pcm[AUDIO_PCM_BLOCKS];
} audio_pcm_pool_t;
enum { AUDIO_PCM_FAILED, AUDIO_PCM_CONTIGUOUS, AUDIO_PCM_SPLIT };
static inline int16_t *audio_pcm_align(void *raw)
{
    return (int16_t *)(((uintptr_t)raw + AUDIO_ALIGN - 1) & ~(uintptr_t)(AUDIO_ALIGN - 1));
}
static inline void audio_pcm_release(audio_pcm_pool_t *pool)
{
    for (unsigned i = 0; i < AUDIO_PCM_BLOCKS; i++) {
        kfree(pool->raw[i]);
        pool->raw[i] = NULL;
        pool->pcm[i] = NULL;
    }
}
static inline int audio_pcm_allocate(audio_pcm_pool_t *pool)
{
    pool->raw[0] = kmalloc_internal(AUDIO_PCM_BLOCKS * AUDIO_PCM_BYTES + AUDIO_ALIGN - 1);
    if (pool->raw[0]) {
        int16_t *base = audio_pcm_align(pool->raw[0]);
        for (unsigned i = 0; i < AUDIO_PCM_BLOCKS; i++)
            pool->pcm[i] = base + i * AUDIO_PCM_FRAMES * 2;
        return AUDIO_PCM_CONTIGUOUS;
    }
    for (unsigned i = 0; i < AUDIO_PCM_BLOCKS; i++) {
        pool->raw[i] = kmalloc_internal(AUDIO_PCM_BYTES + AUDIO_ALIGN - 1);
        if (!pool->raw[i]) {
            audio_pcm_release(pool);
            return AUDIO_PCM_FAILED;
        }
        pool->pcm[i] = audio_pcm_align(pool->raw[i]);
    }
    return AUDIO_PCM_SPLIT;
}
