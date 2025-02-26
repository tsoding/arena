#ifdef __wasm__
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
#endif // __wasm__

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
} Lexer;

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

void report_lexer_location(Lexer *lexer)
{
    int n = lexer->cursor - lexer->begin;
    printf("%s\n", lexer->begin);
    printf("%*s^\n", n, "");
}

void report_current_character(Lexer *lexer)
{
    if (*lexer->cursor == '\0') {
        printf("end of source");
    } else if (is_print(*lexer->cursor)) {
        printf("character '%c'", *lexer->cursor);
    } else {
        printf("byte %02x", (unsigned char)*lexer->cursor);
    }
}

void report_unexpected_error(Lexer *lexer, const char *what_was_expected)
{
    report_lexer_location(lexer);
    printf("ERROR: Unexpected ");
    report_current_character(lexer);
    printf(". Expected %s.\n", what_was_expected);
}

Node *parse_expr(Lexer *lexer);

Node *parse_primary(Lexer *lexer)
{
    if (*lexer->cursor == '(') {
        lexer->cursor += 1;
        Node *expr = parse_expr(lexer);
        if (expr == NULL) return NULL;
        if (*lexer->cursor != ')') {
            report_unexpected_error(lexer, "')'");
            return NULL;
        }
        lexer->cursor += 1;
        return expr;
    } else if (is_digit(*lexer->cursor)) {
        unsigned long long n = 0;
        while (is_digit(*lexer->cursor)) {
            n = n*10 + *lexer->cursor - '0';
            lexer->cursor += 1;
        }
        return node_number(n);
    } else {
        report_unexpected_error(lexer, "'(' or a number");
        return NULL;
    }
}

Node *parse_binop(Lexer *lexer)
{
    Node *lhs = parse_primary(lexer);
    if (lhs == NULL) return NULL;
    if (*lexer->cursor != '+' && *lexer->cursor != '*') return lhs;
    Node *node = arena_alloc(&nodes, sizeof(Node));
    node->binop.lhs = lhs;
    switch (*lexer->cursor) {
    case '+': node->kind = NK_PLUS; break;
    case '*': node->kind = NK_MULT; break;
    default:  ARENA_ASSERT(false && "UNREACHABLE");
    }
    lexer->cursor += 1;
    node->binop.rhs = parse_expr(lexer);
    if (node->binop.rhs == NULL) return NULL;
    return node;
}

Node *parse_expr(Lexer *lexer)
{
    return parse_binop(lexer);
}

Lexer cstr_to_lexer(const char *cstr)
{
    return (Lexer) {
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
    Lexer lexer = cstr_to_lexer("((2*17)+(10*3))+(5*(1+1))");
    printf("Source: %s\n", lexer.begin);
    Node *expr = parse_expr(&lexer);
    if (expr == NULL) return 1;
    if (*lexer.cursor != '\0') {
        report_unexpected_error(&lexer, "end of source");
        return 1;
    }
    printf("Parsed AST:\n");
    node_print(expr, 1);
    printf("\n");

    // Just making several wasm32 page size allocations to test the wasm32 memory grow.
    // x86_64 should not care because it's not a toy platform unlike wasm.
    arena_alloc(&nodes, 64*1024);
    arena_alloc(&nodes, 64*1024);
    arena_alloc(&nodes, 64*1024);
    arena_alloc(&nodes, 64*1024);

    printf("Arena Summary:\n");
    printf("  Default region size: %d words (%zu bytes)\n", ARENA_REGION_DEFAULT_CAPACITY, ARENA_REGION_DEFAULT_CAPACITY*sizeof(uintptr_t));
    printf("  Regions (%zu):\n", count_regions(nodes));
    size_t n = 0;
    for (Region *iter = nodes.begin; iter != NULL; iter = iter->next) {
        printf("    Region %zu: address = %p, capacity = %zu words (%zu bytes), count = %zu words (%zu bytes)\n",
               n, iter, iter->capacity, iter->capacity*sizeof(uintptr_t), iter->count, iter->count*sizeof(uintptr_t));
        n += 1;
    }

    return 0;
}
