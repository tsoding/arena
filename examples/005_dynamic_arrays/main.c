#include <stdio.h>
#define ARENA_IMPLEMENTATION
#include "arena.h"

// TODO: add more examples for arena_da_append and arena_da_append_many

// String Builder is any structure that has at least three fields: items, count, and capacity.
// - `items` MUST be a pointer to char and it points to the beginning of the buffer that stores the string.
// - `count` MUST be size_t and it indicates the size of the string.
// - `capacity` MUST be size_t and it indicates how much memory was already allocated for the String Builder.
// String Builder in a sense is just a Dynamic Array of characters
typedef struct {
    char *items;
    size_t count;
    size_t capacity;
} String_Builder;

int main()
{
    char name[] = {'W', 'o', 'r', 'l', 'd'};

    Arena a = {0};
    String_Builder sb = {0};

    // Append NULL-terminated string
    arena_sb_append_cstr(&a, &sb, "Hello, ");

    // Append sized buffer
    arena_sb_append_buf(&a, &sb, name, sizeof(name));

    // Append '\0' to make the built string NULL-terminated
    arena_sb_append_null(&a, &sb);

    printf("%s\n", sb.items);

    return 0;
}
