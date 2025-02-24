# Expression Parser

Classical example of parsing an AST of an expression allocating nodes in an arena so to no worry about deallocating each and individual node separately.

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
