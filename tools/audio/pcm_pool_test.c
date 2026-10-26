#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../kernel/memory/heap.h"
#include "../../drivers/audio/pcm_pool.h"
static heap_t heap;
static unsigned char arena[192 * 1024] __attribute__((aligned(128)));
static unsigned calls, fail_at;
static bool force_split;
uint32_t arch_irq_disable(void) { return 0; }
void arch_irq_restore(uint32_t flags) { (void)flags; }
_Noreturn void kernel_panic(const char *fmt, ...) { (void)fmt; abort(); }
void *kmalloc_internal(size_t size)
{
    calls++;
    if ((force_split && calls == 1) || calls == fail_at) return NULL;
    return heap_alloc(&heap, size);
}
void kfree(void *p) { if (p) heap_free(&heap, p); }
static kmem_stats_t stats(void) { kmem_stats_t s; heap_stats(&heap, &s); return s; }
static void restored(kmem_stats_t before)
{
    kmem_stats_t after = stats();
    assert(heap_check(&heap));
    assert(after.used == before.used && after.free == before.free);
    assert(after.largest_free == before.largest_free);
}
static void check_blocks(audio_pcm_pool_t *p)
{
    for (unsigned i = 0; i < AUDIO_PCM_BLOCKS; i++) {
        assert(p->pcm[i] && !((uintptr_t)p->pcm[i] & (AUDIO_ALIGN - 1)));
        assert(heap_contains(&heap, p->pcm[i]));
        memset(p->pcm[i], (int)i + 1, AUDIO_PCM_BYTES);
    }
    for (unsigned i = 0; i < AUDIO_PCM_BLOCKS; i++)
        for (unsigned n = 0; n < AUDIO_PCM_BYTES; n++)
            assert(((unsigned char *)p->pcm[i])[n] == i + 1);
    assert(heap_check(&heap));
}
int main(void)
{
    heap_init(&heap, "test internal", arena, sizeof arena);
    audio_pcm_pool_t p = {0};
    kmem_stats_t before = stats();
    for (unsigned n = 0; n < 20; n++) {
        calls = 0; force_split = n % 2;
        assert(audio_pcm_allocate(&p) == (force_split ? AUDIO_PCM_SPLIT : AUDIO_PCM_CONTIGUOUS));
        check_blocks(&p);
        audio_pcm_release(&p); audio_pcm_release(&p);
        restored(before);
    }
    /* Every partial split failure must return all allocations and allow retry. */
    force_split = true;
    for (unsigned i = 0; i < AUDIO_PCM_BLOCKS; i++) {
        calls = 0; fail_at = i + 2;
        assert(audio_pcm_allocate(&p) == AUDIO_PCM_FAILED);
        for (unsigned j = 0; j < AUDIO_PCM_BLOCKS; j++) assert(!p.raw[j] && !p.pcm[j]);
        restored(before);
        calls = fail_at = 0;
        assert(audio_pcm_allocate(&p) == AUDIO_PCM_SPLIT);
        check_blocks(&p); audio_pcm_release(&p); restored(before);
    }
    force_split = false;
    /* Real first-fit heap: eight separated holes, no large contiguous tail. */
    void *holes[AUDIO_PCM_BLOCKS], *guards[AUDIO_PCM_BLOCKS];
    for (unsigned i = 0; i < AUDIO_PCM_BLOCKS; i++) {
        holes[i] = heap_alloc(&heap, AUDIO_PCM_BYTES + AUDIO_ALIGN - 1);
        guards[i] = heap_alloc(&heap, 256);
        assert(holes[i] && guards[i]); memset(guards[i], 0xa5, 256);
    }
    void *tail = heap_alloc(&heap, stats().largest_free); assert(tail);
    for (unsigned i = 0; i < AUDIO_PCM_BLOCKS; i++) heap_free(&heap, holes[i]);
    kmem_stats_t fragmented = stats();
    assert(fragmented.free > AUDIO_PCM_BYTES * AUDIO_PCM_BLOCKS);
    assert(fragmented.largest_free < AUDIO_PCM_BYTES * AUDIO_PCM_BLOCKS);
    calls = 0;
    assert(audio_pcm_allocate(&p) == AUDIO_PCM_SPLIT);
    check_blocks(&p);
    for (unsigned i = 0; i < AUDIO_PCM_BLOCKS; i++)
        for (unsigned n = 0; n < 256; n++) assert(((unsigned char *)guards[i])[n] == 0xa5);
    audio_pcm_release(&p); restored(fragmented);
    heap_free(&heap, tail);
    for (unsigned i = 0; i < AUDIO_PCM_BLOCKS; i++) heap_free(&heap, guards[i]);
    restored(before);
    puts("audio PCM pool: real fragmented heap, alignment, nonoverlap, rollback and reopen PASS");
}
