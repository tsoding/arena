# Arena Allocator

[Arena Allocator](https://en.wikipedia.org/wiki/Region-based_memory_management) implementation in pure C as an [stb-style single-file library](https://github.com/nothings/stb).

*I just caught myself implementing this over and over again in my projects, so I decided to turn it into a copy-pastable library similar to [sv](http://github.com/tsoding/sv)*

## Quick Start

> The truly reusable code is the one that you can simply copy-paste.

The library itself does not require any special building. You can simple copy-paste [./arena.h](./arena.h) to your project and `#include` it.

```c
#define ARENA_IMPLEMENTATION
#include "arena.h"

static Arena default_arena = {0};
static Arena temporary_arena = {0};
static Arena *context_arena = &default_arena;

void *context_alloc(size_t size)
{
    assert(context_arena);
    return arena_alloc(context_arena, size);
}

int main(void)
{
    // Allocate stuff in default_arena
    context_alloc(64);
    context_alloc(128);
    context_alloc(256);
    context_alloc(512);

    // Allocate stuff in temporary_arena;
    context_arena = &temporary_arena;
    context_alloc(64);
    context_alloc(128);
    context_alloc(256);
    context_alloc(512);

    // Deallocate everything at once
    arena_free(&default_arena);
    arena_free(&temporary_arena);
    return 0;
}
```
