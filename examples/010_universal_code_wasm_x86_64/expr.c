#ifdef PLATFORM_WASM
    #include <stdarg.h>
    #define STB_SPRINTF_IMPLEMENTATION
    #include "stb_sprintf.h"
    #define WRITE_BUFFER_CAPACITY 4096
    char write_buffer[WRITE_BUFFER_CAPACITY];
    void platform_write(void *buffer, size_t len);
    int printf(const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        int n = stbsp_vsnprintf(write_buffer, WRITE_BUFFER_CAPACITY, fmt, args);
        va_end(args);
        platform_write(write_buffer, n);
        return n;
    }
    #define ARENA_BACKEND ARENA_BACKEND_WASM_HEAPBASE
    #define ARENA_NOSTDIO
    // We are using __builtin_trap() because we are assuming only clang can compile to wasm as of today
    #define ARENA_ASSERT(cond) (!(cond) ? printf("%s:%d: %s: Assertion `%s' failed.", __FILE__, __LINE__, __func__, #cond), __builtin_trap() : 0)
#else
    #include <stdio.h>
#endif // PLATFORM_WASM

#include <stdbool.h>

// Implementing or own is_digit and is_print, because ctype is not available on wasm32 in clang for some reason
bool is_digit(char x)
{
    return '0' <= x && x <= '9';
}
bool is_print(char x)
{
    // Stolen from musl
    return (unsigned)x-0x20 < 0x5f;
}

#define ARENA_REGION_DEFAULT_CAPACITY 10
#define ARENA_IMPLEMENTATION
#include "arena.h"

static Arena nodes = {0};

typedef enum {
    NK_NUMB,
    NK_PLUS,
    NK_MULT,
} Node_Kind;

typedef struct Node Node;

typedef struct {
    Node *lhs;
    Node *rhs;
} Node_Binop;

struct Node {
    Node_Kind kind;
    union {
        int number;
        Node_Binop binop;
    };
};

Node *node_number(int number)
{
    Node *node = arena_alloc(&nodes, sizeof(Node));
    node->kind = NK_NUMB;
    node->number = number;
    return node;
}

typedef struct {
    const char *begin;
    const char *cursor;
} Source;

void node_print(Node *root, int level)
{
    switch (root->kind) {
    case NK_NUMB:
        printf("%*s- number: %d\n", 2*level, "", root->number);
        break;
    case NK_PLUS:
        printf("%*s- plus:\n", 2*level, "");
        node_print(root->binop.lhs, level + 1);
        node_print(root->binop.rhs, level + 1);
        break;
    case NK_MULT:
        printf("%*s- mult:\n", 2*level, "");
        node_print(root->binop.lhs, level + 1);
        node_print(root->binop.rhs, level + 1);
        break;
    default:
        ARENA_ASSERT(false && "UNREACHABLE");
    }
}

void report_source_location(Source *src)
{
    int n = src->cursor - src->begin;
    printf("%s\n", src->begin);
    printf("%*s^\n", n, "");
}

void report_current_character(Source *src)
{
    if (*src->cursor == '\0') {
        printf("end of source");
    } else if (is_print(*src->cursor)) {
        printf("character '%c'", *src->cursor);
    } else {
        printf("byte %02x", (unsigned char)*src->cursor);
    }
}

void report_unexpected_error(Source *src, const char *what_was_expected)
{
    report_source_location(src);
    printf("ERROR: Unexpected ");
    report_current_character(src);
    printf(". Expected %s.\n", what_was_expected);
}

Node *parse_expr(Source *src);

Node *parse_primary(Source *src)
{
    if (*src->cursor == '(') {
        src->cursor += 1;
        Node *expr = parse_expr(src);
        if (expr == NULL) return NULL;
        if (*src->cursor != ')') {
            report_unexpected_error(src, "')'");
            return NULL;
        }
        src->cursor += 1;
        return expr;
    } else if (is_digit(*src->cursor)) {
        unsigned long long n = 0;
        while (is_digit(*src->cursor)) {
            n = n*10 + *src->cursor - '0';
            src->cursor += 1;
        }
        return node_number(n);
    } else {
        report_unexpected_error(src, "'(' or a number");
        return NULL;
    }
}

Node *parse_binop(Source *src)
{
    Node *lhs = parse_primary(src);
    if (lhs == NULL) return NULL;
    if (*src->cursor != '+' && *src->cursor != '*') return lhs;
    Node *node = arena_alloc(&nodes, sizeof(Node));
    node->binop.lhs = lhs;
    switch (*src->cursor) {
    case '+': node->kind = NK_PLUS; break;
    case '*': node->kind = NK_MULT; break;
    default:  ARENA_ASSERT(false && "UNREACHABLE");
    }
    src->cursor += 1;
    node->binop.rhs = parse_expr(src);
    if (node->binop.rhs == NULL) return NULL;
    return node;
}

Node *parse_expr(Source *src)
{
    return parse_binop(src);
}

Source cstr_to_src(const char *cstr)
{
    return (Source) {
        .begin = cstr,
        .cursor = cstr,
    };
}

size_t count_regions(Arena a)
{
    size_t n = 0;
    for (Region *iter = a.begin; iter != NULL; iter = iter->next) {
        n++;
    }
    return n;
}

int main(void)
{
    Source src = cstr_to_src("((2*17)+(10*3))+(5*(1+1))");
    printf("Source: %s\n", src.begin);
    Node *expr = parse_expr(&src);
    if (expr == NULL) return 1;
    if (*src.cursor != '\0') {
        report_unexpected_error(&src, "end of source");
        return 1;
    }
    printf("Parsed AST:\n");
    node_print(expr, 1);

    printf("\n");
    printf("Arena Summary:\n");
    printf("  Default region size: %d words (%zu bytes)\n", ARENA_REGION_DEFAULT_CAPACITY, ARENA_REGION_DEFAULT_CAPACITY*sizeof(uintptr_t));
    printf("  Regions (%zu):\n", count_regions(nodes));
    size_t n = 0;
    for (Region *iter = nodes.begin; iter != NULL; iter = iter->next) {
        printf("    Region %zu: address = %p, capacity = %zu words (%zu bytes), count = %zu words (%zu bytes)\n",
               n, iter, iter->capacity, iter->capacity*sizeof(uintptr_t), iter->count, iter->count*sizeof(uintptr_t));
        n += 1;
    }

    // Just making a wasm32 page size allocation to test the wasm32 memory grow.
    // x86_64 should not care because it's not a toy platform.
    arena_alloc(&nodes, 64*1024);

    return 0;
}
