# Expression Parser

This example demonstrates how to write universal code that works both on WASM and x86_64.

Here we implemented a classical example of parsing an AST of an expression allocating nodes in an arena so to no worry about deallocating each and individual node separately.

## Quick Start

Build:

```console
$ clang -o nob nob.c
$ ./nob
```

Native example:

```console
$ ./build/expr.native
```

WASM example:

```console
$ python3 -m http.server 6969
$ firefox http://localhost:6969/expr.html
```
