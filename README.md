# Anchor

A statically-typed programming language that transpiles to C99.

```anchor
from core.math import abs

struct Pair
    a: int
    b: int

    func hash(): int
        return self.a ^ self.b
    end
end

interface Hashable
    func hash(): int
end

func get_hash(h: &Hashable): int
    return h.hash()
end

func main(): int
    var p = Pair(a = 3, b = 9)
    return get_hash(&p)
end
```

## Features

- Clean syntax with `end`-delimited blocks
- Structs with methods, enums, and interfaces with implicit structural matching
- Generics with monomorphization
- References (`&T`) and nullable pointers (`*T`) with pointer arithmetic
- Scoped resource management via `with` / `release()`
- Module system with `export` / `import`
- Extern FFI to C functions
- Transpiles to readable C99, compiled with gcc/clang/cl

## Prerequisites

A C compiler must be installed and available in your `PATH`:
gcc, clang, or cl (MSVC).

## Build

```sh
cmake -B build
cmake --build build
```

## Usage

Run a single file:

```sh
bin/ancc.exe run file.anc
```

Build a multi-file package:

```sh
bin/ancc.exe build path/to/project
```

## Testing

```sh
py tests/run_tests.py
```

Run a single test by name:

```sh
py tests/run_tests.py --filter enum_basic
```

## Documentation

- [Getting Started](docs/getting-started.md)
- [Language Reference](docs/reference.md)
- [Tooling](docs/tooling.md)
