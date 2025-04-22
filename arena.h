// Copyright 2022 Alexey Kutepov <reximkut@gmail.com>

// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:

// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef ARENA_H_
#define ARENA_H_

#include <stddef.h>
#include <stdint.h>

#ifndef ARENA_NOSTDIO
#include <stdarg.h>
#include <stdio.h>
#endif // ARENA_NOSTDIO

#ifndef ARENA_ASSERT
#include <assert.h>
#define ARENA_ASSERT assert
#endif

#define ARENA_BACKEND_LIBC_MALLOC 0
#define ARENA_BACKEND_LINUX_MMAP 1
#define ARENA_BACKEND_WIN32_VIRTUALALLOC 2
#define ARENA_BACKEND_WASM_HEAPBASE 3

#ifndef ARENA_BACKEND
#define ARENA_BACKEND ARENA_BACKEND_LIBC_MALLOC
#endif // ARENA_BACKEND

typedef struct Region Region;

struct Region {
    Region *next;
    size_t count;
    size_t capacity;
    uintptr_t data[];
};

typedef struct {
    Region *begin, *end;
} Arena;

typedef struct  {
    Region *region;
    size_t count;
} Arena_Mark;

#ifndef ARENA_REGION_DEFAULT_CAPACITY
#define ARENA_REGION_DEFAULT_CAPACITY (8*1024)
#endif // ARENA_REGION_DEFAULT_CAPACITY

Region *new_region(size_t capacity);
void free_region(Region *r);

void *arena_alloc(Arena *a, size_t size_bytes);
void *arena_realloc(Arena *a, void *oldptr, size_t oldsz, size_t newsz);
char *arena_strdup(Arena *a, const char *cstr);
void *arena_memdup(Arena *a, void *data, size_t size);
void *arena_memcpy(void *dest, const void *src, size_t n);
#ifndef ARENA_NOSTDIO
char *arena_sprintf(Arena *a, const char *format, ...);
char *arena_vsprintf(Arena *a, const char *format, va_list args);
#endif // ARENA_NOSTDIO

Arena_Mark arena_snapshot(Arena *a);
void arena_reset(Arena *a);
void arena_rewind(Arena *a, Arena_Mark m);
void arena_free(Arena *a);
void arena_trim(Arena *a);

#ifndef ARENA_DA_INIT_CAP
#define ARENA_DA_INIT_CAP 256
#endif // ARENA_DA_INIT_CAP

#ifdef __cplusplus
    #define cast_ptr(ptr) (decltype(ptr))
#else
    #define cast_ptr(...)
#endif

#define arena_da_append(a, da, item)                                                          \
    do {                                                                                      \
        if ((da)->count >= (da)->capacity) {                                                  \
            size_t new_capacity = (da)->capacity == 0 ? ARENA_DA_INIT_CAP : (da)->capacity*2; \
            (da)->items = cast_ptr((da)->items)arena_realloc(                                 \
                (a), (da)->items,                                                             \
                (da)->capacity*sizeof(*(da)->items),                                          \
                new_capacity*sizeof(*(da)->items));                                           \
            (da)->capacity = new_capacity;                                                    \
        }                                                                                     \
                                                                                              \
        (da)->items[(da)->count++] = (item);                                                  \
    } while (0)

// Append several items to a dynamic array
#define arena_da_append_many(a, da, new_items, new_items_count)                                       \
    do {                                                                                              \
        if ((da)->count + (new_items_count) > (da)->capacity) {                                       \
            size_t new_capacity = (da)->capacity;                                                     \
            if (new_capacity == 0) new_capacity = ARENA_DA_INIT_CAP;                                  \
            while ((da)->count + (new_items_count) > new_capacity) new_capacity *= 2;                 \
            (da)->items = cast_ptr((da)->items)arena_realloc(                                         \
                (a), (da)->items,                                                                     \
                (da)->capacity*sizeof(*(da)->items),                                                  \
                new_capacity*sizeof(*(da)->items));                                                   \
            (da)->capacity = new_capacity;                                                            \
        }                                                                                             \
        arena_memcpy((da)->items + (da)->count, (new_items), (new_items_count)*sizeof(*(da)->items)); \
        (da)->count += (new_items_count);                                                             \
    } while (0)

// Append a sized buffer to a string builder
#define arena_sb_append_buf arena_da_append_many

// Append a NULL-terminated string to a string builder
#define arena_sb_append_cstr(a, sb, cstr)  \
    do {                                   \
        const char *s = (cstr);            \
        size_t n = arena_strlen(s);        \
        arena_da_append_many(a, sb, s, n); \
    } while (0)

// Append a single NULL character at the end of a string builder. So then you can
// use it a NULL-terminated C string
#define arena_sb_append_null(a, sb) arena_da_append(a, sb, 0)

#endif // ARENA_H_

#ifdef ARENA_IMPLEMENTATION

#if ARENA_BACKEND == ARENA_BACKEND_LIBC_MALLOC
#include <stdlib.h>

// TODO: instead of accepting specific capacity new_region() should accept the size of the object we want to fit into the region
// It should be up to new_region() to decide the actual capacity to allocate
Region *new_region(size_t capacity)
{
    size_t size_bytes = sizeof(Region) + sizeof(uintptr_t)*capacity;
    // TODO: it would be nice if we could guarantee that the regions are allocated by ARENA_BACKEND_LIBC_MALLOC are page aligned
    Region *r = (Region*)malloc(size_bytes);
    ARENA_ASSERT(r); // TODO: since ARENA_ASSERT is disableable go through all the places where we use it to check for failed memory allocation and return with NULL there.
    r->next = NULL;
    r->count = 0;
    r->capacity = capacity;
    return r;
}

void free_region(Region *r)
{
    free(r);
}
#elif ARENA_BACKEND == ARENA_BACKEND_LINUX_MMAP
#include <unistd.h>
#include <sys/mman.h>

Region *new_region(size_t capacity)
{
    size_t size_bytes = sizeof(Region) + sizeof(uintptr_t) * capacity;
    Region *r = mmap(NULL, size_bytes, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    ARENA_ASSERT(r != MAP_FAILED);
    r->next = NULL;
    r->count = 0;
    r->capacity = capacity;
    return r;
}

void free_region(Region *r)
{
    size_t size_bytes = sizeof(Region) + sizeof(uintptr_t) * r->capacity;
    int ret = munmap(r, size_bytes);
    ARENA_ASSERT(ret == 0);
}

#elif ARENA_BACKEND == ARENA_BACKEND_WIN32_VIRTUALALLOC

#if !defined(_WIN32)
#  error "Current platform is not Windows"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define INV_HANDLE(x)       (((x) == NULL) || ((x) == INVALID_HANDLE_VALUE))

Region *new_region(size_t capacity)
{
    SIZE_T size_bytes = sizeof(Region) + sizeof(uintptr_t) * capacity;
    Region *r = VirtualAllocEx(
        GetCurrentProcess(),      /* Allocate in current process address space */
        NULL,                     /* Unknown position */
        size_bytes,               /* Bytes to allocate */
        MEM_COMMIT | MEM_RESERVE, /* Reserve and commit allocated page */
        PAGE_READWRITE            /* Permissions ( Read/Write )*/
    );
    if (INV_HANDLE(r))
        ARENA_ASSERT(0 && "VirtualAllocEx() failed.");

    r->next = NULL;
    r->count = 0;
    r->capacity = capacity;
    return r;
}

void free_region(Region *r)
{
    if (INV_HANDLE(r))
        return;

    BOOL free_result = VirtualFreeEx(
        GetCurrentProcess(),        /* Deallocate from current process address space */
        (LPVOID)r,                  /* Address to deallocate */
        0,                          /* Bytes to deallocate ( Unknown, deallocate entire page ) */
        MEM_RELEASE                 /* Release the page ( And implicitly decommit it ) */
    );

    if (FALSE == free_result)
        ARENA_ASSERT(0 && "VirtualFreeEx() failed.");
}

#elif ARENA_BACKEND == ARENA_BACKEND_WASM_HEAPBASE

// Stolen from https://surma.dev/things/c-to-webassembly/

extern unsigned char __heap_base;
// Since ARENA_BACKEND_WASM_HEAPBASE entirely hijacks __heap_base it is expected that no other means of memory
// allocation are used except the arenas.
unsigned char* bump_pointer = &__heap_base;
// TODO: provide a way to deallocate all the arenas at once by setting bump_pointer back to &__heap_base?

// __builtin_wasm_memory_size and __builtin_wasm_memory_grow are defined in units of page sizes
#define ARENA_WASM_PAGE_SIZE (64*1024)

Region *new_region(size_t capacity)
{
    size_t size_bytes = sizeof(Region) + sizeof(uintptr_t)*capacity;
    Region *r = (void*)bump_pointer;

    // grow memory brk() style
    size_t current_memory_size = ARENA_WASM_PAGE_SIZE * __builtin_wasm_memory_size(0);
    size_t desired_memory_size = (size_t) bump_pointer + size_bytes;
    if (desired_memory_size > current_memory_size) {
        size_t delta_bytes = desired_memory_size - current_memory_size;
        size_t delta_pages = (delta_bytes + (ARENA_WASM_PAGE_SIZE - 1))/ARENA_WASM_PAGE_SIZE;
        if (__builtin_wasm_memory_grow(0, delta_pages) < 0) {
            ARENA_ASSERT(0 && "memory.grow failed");
            return NULL;
        }
    }

    bump_pointer += size_bytes;

    r->next = NULL;
    r->count = 0;
    r->capacity = capacity;
    return r;
}

void free_region(Region *r)
{
    // Since ARENA_BACKEND_WASM_HEAPBASE uses a primitive bump allocator to
    // allocate the regions, free_region() does nothing. It is generally
    // not recommended to free arenas anyway since it is better to keep
    // reusing already allocated memory with arena_reset().
    (void) r;
}

#else
#  error "Unknown Arena backend"
#endif

// TODO: add debug statistic collection mode for arena
// Should collect things like:
// - How many times new_region was called
// - How many times existing region was skipped
// - How many times allocation exceeded ARENA_REGION_DEFAULT_CAPACITY

void *arena_alloc(Arena *a, size_t size_bytes)
{
    size_t size = (size_bytes + sizeof(uintptr_t) - 1)/sizeof(uintptr_t);

    if (a->end == NULL) {
        ARENA_ASSERT(a->begin == NULL);
        size_t capacity = ARENA_REGION_DEFAULT_CAPACITY;
        if (capacity < size) capacity = size;
        a->end = new_region(capacity);
        a->begin = a->end;
    }

    while (a->end->count + size > a->end->capacity && a->end->next != NULL) {
        a->end = a->end->next;
    }

    if (a->end->count + size > a->end->capacity) {
        ARENA_ASSERT(a->end->next == NULL);
        size_t capacity = ARENA_REGION_DEFAULT_CAPACITY;
        if (capacity < size) capacity = size;
        a->end->next = new_region(capacity);
        a->end = a->end->next;
    }

    void *result = &a->end->data[a->end->count];
    a->end->count += size;
    return result;
}

void *arena_realloc(Arena *a, void *oldptr, size_t oldsz, size_t newsz)
{
    if (newsz <= oldsz) return oldptr;
    void *newptr = arena_alloc(a, newsz);
    char *newptr_char = (char*)newptr;
    char *oldptr_char = (char*)oldptr;
    for (size_t i = 0; i < oldsz; ++i) {
        newptr_char[i] = oldptr_char[i];
    }
    return newptr;
}

size_t arena_strlen(const char *s)
{
    size_t n = 0;
    while (*s++) n++;
    return n;
}

void *arena_memcpy(void *dest, const void *src, size_t n)
{
    char *d = dest;
    const char *s = src;
    for (; n; n--) *d++ = *s++;
    return dest;
}

char *arena_strdup(Arena *a, const char *cstr)
{
    size_t n = arena_strlen(cstr);
    char *dup = (char*)arena_alloc(a, n + 1);
    arena_memcpy(dup, cstr, n);
    dup[n] = '\0';
    return dup;
}

void *arena_memdup(Arena *a, void *data, size_t size)
{
    return arena_memcpy(arena_alloc(a, size), data, size);
}

#ifndef ARENA_NOSTDIO
char *arena_vsprintf(Arena *a, const char *format, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);
    int n = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    ARENA_ASSERT(n >= 0);
    char *result = (char*)arena_alloc(a, n + 1);
    vsnprintf(result, n + 1, format, args);

    return result;
}

char *arena_sprintf(Arena *a, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char *result = arena_vsprintf(a, format, args);
    va_end(args);

    return result;
}
#endif // ARENA_NOSTDIO

Arena_Mark arena_snapshot(Arena *a)
{
    Arena_Mark m;
    if(a->end == NULL){ //snapshot of uninitialized arena
        ARENA_ASSERT(a->begin == NULL);
        m.region = a->end;
        m.count  = 0;
    }else{
        m.region = a->end;
        m.count  = a->end->count;
    }

    return m;
}

void arena_reset(Arena *a)
{
    for (Region *r = a->begin; r != NULL; r = r->next) {
        r->count = 0;
    }

    a->end = a->begin;
}

void arena_rewind(Arena *a, Arena_Mark m)
{
    if(m.region == NULL){ //snapshot of uninitialized arena
        arena_reset(a);   //leave allocation
        return;
    }

    m.region->count = m.count;
    for (Region *r = m.region->next; r != NULL; r = r->next) {
        r->count = 0;
    }

    a->end = m.region;
}

void arena_free(Arena *a)
{
    Region *r = a->begin;
    while (r) {
        Region *r0 = r;
        r = r->next;
        free_region(r0);
    }
    a->begin = NULL;
    a->end = NULL;
}

void arena_trim(Arena *a){
    Region *r = a->end->next;
    while (r) {
        Region *r0 = r;
        r = r->next;
        free_region(r0);
    }
    a->end->next = NULL;
}

#endif // ARENA_IMPLEMENTATION
