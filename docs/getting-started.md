# Getting Started

Anchor is a statically-typed programming language that transpiles to C99.

## Prerequisites

Anchor transpiles to C99 and needs a C compiler to produce binaries. One of the following must be installed and available in your `PATH`:

- **gcc** (Linux, macOS, MinGW)
- **clang** (Linux, macOS)
- **cl** (MSVC on Windows)

## Installation

Build the compiler from source:

```sh
cmake -B build
cmake --build build
```

This produces the `bin/ancc.exe` compiler binary.

## Hello World

Create a file called `hello.anc`:

```anchor
func main(): int
    return 0
end
```

Compile and run it:

```sh
bin/ancc.exe run hello.anc
```

## A Larger Example

```anchor
struct Point
    x: int
    y: int

    func manhattan(): int
        var ax = self.x
        if ax < 0
            ax = -ax
        end
        var ay = self.y
        if ay < 0
            ay = -ay
        end
        return ax + ay
    end
end

func main(): int
    var p = Point(x = 3, y = -4)
    return p.manhattan()
end
```

## Multi-File Projects

For projects with multiple files, create a package. A package needs a manifest file named `anchor` (no extension) in the project root:

```
name myproject
entry main
```

Organize source files under `src/`:

```
myproject/
    anchor
    src/
        main.anc
        utils.anc
        core/
            math.anc
```

Import symbols between modules using dot-separated paths that map to directories:

```anchor
# In src/main.anc
from utils import sign
from core.math import abs

func main(): int
    return abs(sign(-5))
end
```

```anchor
# In src/utils.anc
export func sign(x: int): int
    if x < 0
        return -1
    elseif x > 0
        return 1
    else
        return 0
    end
end
```

```anchor
# In src/core/math.anc
export func abs(x: int): int
    if x < 0
        return -x
    else
        return x
    end
end
```

Build the project:

```sh
bin/ancc.exe build myproject
```

Only symbols marked with `export` are visible to other modules.
