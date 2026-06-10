/*
 * MultiRexZ80
 *
 * Multi-system Z80 emulator based on SMS Plus GX by Eke-Eke, itself based on
 * SMS Plus by Charles MacDonald.
 *
 * Default project license: GPL-2.0-or-later.  File-specific notices below
 * are retained and take precedence for imported or derived components,
 * including MAME-derived code and other third-party modules.
 */

#define MULTIREXZ80_STATIC_ALLOC_NO_MACROS
#include "static_alloc.h"

#if defined(MULTIREXZ80_STATIC_MEMORY)
#include <stdint.h>
#include <string.h>

#ifndef MULTIREXZ80_STATIC_MEMORY_BYTES
#define MULTIREXZ80_STATIC_MEMORY_BYTES (16u * 1024u * 1024u)
#endif

#define MULTIREXZ80_STATIC_ALIGN ((size_t)sizeof(uintptr_t) > 8u ? (size_t)sizeof(uintptr_t) : 8u)
#define MULTIREXZ80_STATIC_MAGIC 0x534D5350u /* SMSP */

typedef struct multirexz80_static_block_s
{
    size_t size;
    struct multirexz80_static_block_s *next;
    uint32_t magic;
    uint8_t free;
} multirexz80_static_block_t;

typedef union
{
    uint64_t align_u64;
    void *align_ptr;
    double align_double;
    uint8_t bytes[MULTIREXZ80_STATIC_MEMORY_BYTES];
} multirexz80_static_arena_t;

static multirexz80_static_arena_t multirexz80_static_arena;
static multirexz80_static_block_t *multirexz80_static_head;
static size_t multirexz80_static_used_bytes;
static size_t multirexz80_static_peak_bytes;

static size_t multirexz80_static_align_up(size_t value)
{
    return (value + (MULTIREXZ80_STATIC_ALIGN - 1u)) & ~(MULTIREXZ80_STATIC_ALIGN - 1u);
}

static void multirexz80_static_init(void)
{
    if (multirexz80_static_head)
        return;

    multirexz80_static_head = (multirexz80_static_block_t *)multirexz80_static_arena.bytes;
    multirexz80_static_head->size = MULTIREXZ80_STATIC_MEMORY_BYTES - sizeof(multirexz80_static_block_t);
    multirexz80_static_head->next = NULL;
    multirexz80_static_head->magic = MULTIREXZ80_STATIC_MAGIC;
    multirexz80_static_head->free = 1;
    multirexz80_static_used_bytes = 0;
    multirexz80_static_peak_bytes = 0;
}

static void multirexz80_static_note_alloc(size_t size)
{
    multirexz80_static_used_bytes += size;
    if (multirexz80_static_used_bytes > multirexz80_static_peak_bytes)
        multirexz80_static_peak_bytes = multirexz80_static_used_bytes;
}

static void multirexz80_static_split(multirexz80_static_block_t *block, size_t size)
{
    uint8_t *base;
    multirexz80_static_block_t *next;
    size_t remaining;

    if (block->size < size + sizeof(multirexz80_static_block_t) + MULTIREXZ80_STATIC_ALIGN)
        return;

    remaining = block->size - size - sizeof(multirexz80_static_block_t);
    base = (uint8_t *)(block + 1);
    next = (multirexz80_static_block_t *)(base + size);
    next->size = remaining;
    next->next = block->next;
    next->magic = MULTIREXZ80_STATIC_MAGIC;
    next->free = 1;

    block->size = size;
    block->next = next;
}

static void multirexz80_static_coalesce(void)
{
    multirexz80_static_block_t *block = multirexz80_static_head;

    while (block && block->next)
    {
        if (block->free && block->next->free)
        {
            multirexz80_static_block_t *next = block->next;
            block->size += sizeof(multirexz80_static_block_t) + next->size;
            block->next = next->next;
        }
        else
        {
            block = block->next;
        }
    }
}

static int multirexz80_static_owns(const void *ptr)
{
    const uint8_t *p = (const uint8_t *)ptr;
    return p >= multirexz80_static_arena.bytes && p < (multirexz80_static_arena.bytes + MULTIREXZ80_STATIC_MEMORY_BYTES);
}

void *multirexz80_static_malloc(size_t size)
{
    multirexz80_static_block_t *block;

    if (!size)
        return NULL;

    multirexz80_static_init();
    size = multirexz80_static_align_up(size);

    for (block = multirexz80_static_head; block; block = block->next)
    {
        if (block->free && block->size >= size)
        {
            multirexz80_static_split(block, size);
            block->free = 0;
            multirexz80_static_note_alloc(block->size);
            return (void *)(block + 1);
        }
    }

    return NULL;
}

void *multirexz80_static_calloc(size_t count, size_t size)
{
    size_t total;
    void *ptr;

    if (count && size > ((size_t)-1) / count)
        return NULL;

    total = count * size;
    ptr = multirexz80_static_malloc(total);
    if (ptr)
        memset(ptr, 0, total);
    return ptr;
}

void multirexz80_static_free(void *ptr)
{
    multirexz80_static_block_t *block;

    if (!ptr)
        return;

    multirexz80_static_init();
    if (!multirexz80_static_owns(ptr))
        return;

    block = ((multirexz80_static_block_t *)ptr) - 1;
    if (block->magic != MULTIREXZ80_STATIC_MAGIC || block->free)
        return;

    block->free = 1;
    if (multirexz80_static_used_bytes >= block->size)
        multirexz80_static_used_bytes -= block->size;
    else
        multirexz80_static_used_bytes = 0;
    multirexz80_static_coalesce();
}

void *multirexz80_static_realloc(void *ptr, size_t size)
{
    multirexz80_static_block_t *block;
    void *new_ptr;
    size_t copy_size;

    if (!ptr)
        return multirexz80_static_malloc(size);
    if (!size)
    {
        multirexz80_static_free(ptr);
        return NULL;
    }

    multirexz80_static_init();
    if (!multirexz80_static_owns(ptr))
        return NULL;

    block = ((multirexz80_static_block_t *)ptr) - 1;
    if (block->magic != MULTIREXZ80_STATIC_MAGIC || block->free)
        return NULL;

    size = multirexz80_static_align_up(size);
    if (block->size >= size)
    {
        size_t old_size = block->size;
        multirexz80_static_split(block, size);
        if (old_size > block->size && multirexz80_static_used_bytes >= old_size - block->size)
            multirexz80_static_used_bytes -= old_size - block->size;
        return ptr;
    }

    if (block->next && block->next->free &&
        block->size + sizeof(multirexz80_static_block_t) + block->next->size >= size)
    {
        size_t old_size = block->size;
        multirexz80_static_block_t *next = block->next;
        block->size += sizeof(multirexz80_static_block_t) + next->size;
        block->next = next->next;
        multirexz80_static_split(block, size);
        multirexz80_static_used_bytes += block->size - old_size;
        if (multirexz80_static_used_bytes > multirexz80_static_peak_bytes)
            multirexz80_static_peak_bytes = multirexz80_static_used_bytes;
        return ptr;
    }

    new_ptr = multirexz80_static_malloc(size);
    if (!new_ptr)
        return NULL;

    copy_size = block->size; /* block->size < size is guaranteed by earlier checks */
    memcpy(new_ptr, ptr, copy_size);
    multirexz80_static_free(ptr);
    return new_ptr;
}

void multirexz80_static_reset(void)
{
    multirexz80_static_head = NULL;
    multirexz80_static_init();
}

size_t multirexz80_static_memory_used(void)
{
    return multirexz80_static_used_bytes;
}

size_t multirexz80_static_memory_peak(void)
{
    return multirexz80_static_peak_bytes;
}

size_t multirexz80_static_memory_capacity(void)
{
    return MULTIREXZ80_STATIC_MEMORY_BYTES;
}

#endif /* MULTIREXZ80_STATIC_MEMORY */
