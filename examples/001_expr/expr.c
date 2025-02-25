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
    #define ARENA_ASSERT(cond) (!(cond) ? printf("%s:%d: %s: Assertion `%s' failed.", __FILE__, __LINE__, __func__, #cond), __builtin_trap() : 0)
#else
    #include <stdio.h>
#endif // PLATFORM_WASM

#include <stdbool.h>

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

Node *node_plus(Node *lhs, Node *rhs)
{
    Node *node = arena_alloc(&nodes, sizeof(Node));
    node->kind = NK_PLUS;
    node->binop.lhs = lhs;
    node->binop.rhs = rhs;
    return node;
}

Node *node_mult(Node *lhs, Node *rhs)
{
    Node *node = arena_alloc(&nodes, sizeof(Node));
    node->kind = NK_MULT;
    node->binop.lhs = lhs;
    node->binop.rhs = rhs;
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
        printf("%*s- %d\n", 2*level, "", root->number);
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

void source_location(Source *src)
{
    int n = src->cursor - src->begin;
    printf("%s\n", src->begin);
    printf("%*s^\n", n, "");
}

Node *parse_expr(Source *src);

bool is_digit(char x)
{
    return '0' <= x && x <= '9';
}

Node *parse_primary(Source *src)
{
    if (*src->cursor == '(') {
        src->cursor += 1;
        Node *expr = parse_expr(src);
        if (expr == NULL) return NULL;
        if (*src->cursor != ')') {
            source_location(src);
            printf("ERROR: expected ')' but got '%c'\n", *src->cursor);
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
    } else if (*src->cursor == '\0'){
        source_location(src);
        printf("ERROR: unexpected end of source\n");
        return NULL;
    } else {
        source_location(src);
        printf("ERROR: unexpected character '%c'. Expected '(' or a number\n", *src->cursor);
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
    case '+':
        node->kind = NK_PLUS;
        break;
    case '*':
        node->kind = NK_MULT;
        break;
    default:
        source_location(src);
        printf("ERROR: unexpected character '%c'. Expected '+' or '*'\n", *src->cursor);
        return NULL;
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

int main(void)
{
    Source src = cstr_to_src("((2*17)+(10*3))+(5*(1+1))");
    printf("Source: %s\n", src.begin);
    Node *expr = parse_expr(&src);
    if (expr == NULL) return 1;
    printf("Parsed AST:\n");
    node_print(expr, 1);

    printf("\n");
    printf("Arena Summary:\n");
    printf("  Regions:\n");
    size_t n = 0;
    for (Region *iter = nodes.begin; iter != NULL; iter = iter->next) {
        printf("    Region %zu: capacity %zu words (%zu bytes), count: %zu words (%zu bytes)\n",
               n,
               iter->capacity, iter->capacity*sizeof(uintptr_t),
               iter->count, iter->count*sizeof(uintptr_t));
        n += 1;
    }
    printf("  Arena allocated %zu regions\n", n);
    printf("  Default region size %d words (%zu bytes)\n", ARENA_REGION_DEFAULT_CAPACITY, ARENA_REGION_DEFAULT_CAPACITY*sizeof(uintptr_t));

    return 0;
}
