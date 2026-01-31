# Anchor Language Reference

## Comments

Single-line comments start with `#`:

```anchor
# This is a comment
var x = 5  # inline comment
```

## Variables and Constants

Variables are declared with `var`. Constants are declared with `const`. An initial value is always required.

```anchor
var count = 0
var name: string = "anchor"

const PI = 3.14f
const MAX_SIZE: int = 1024
```

Variables are mutable. Constants are not.

## Types

### Primitive Types

| Anchor type | C99 type     | Description              |
|-------------|--------------|--------------------------|
| `bool`      | `bool`       | Boolean                  |
| `byte`      | `uint8_t`    | Unsigned 8-bit           |
| `short`     | `int16_t`    | Signed 16-bit            |
| `ushort`    | `uint16_t`   | Unsigned 16-bit          |
| `int`       | `int32_t`    | Signed 32-bit            |
| `uint`      | `uint32_t`   | Unsigned 32-bit          |
| `long`      | `int64_t`    | Signed 64-bit            |
| `ulong`     | `uint64_t`   | Unsigned 64-bit          |
| `isize`     | `ptrdiff_t`  | Signed pointer-sized     |
| `usize`     | `size_t`     | Unsigned pointer-sized   |
| `float`     | `float`      | 32-bit floating point    |
| `double`    | `double`     | 64-bit floating point    |
| `string`    | fat pointer  | Pointer + length pair    |

### Strings

Strings are fat pointers with a `.ptr` and `.len` field:

```anchor
var s = "hello"
var p: *byte = s.ptr
var n: usize = s.len
```

### References and Pointers

References (`&T`) are non-nullable. Pointers (`*T`) are nullable.

```anchor
var x = 42
var ref = &x          # type is &int
var ptr: *int = &x    # pointer, can be null
var np: *int = null   # null pointer
```

Dereference with `*`:

```anchor
var val = *ptr        # read through pointer
*ptr = 10             # write through pointer
```

Both references and pointers use dot syntax for field access:

```anchor
var p = Point(x = 3, y = 7)
var ref = &p
var v = ref.x         # no special arrow syntax needed
```

References convert implicitly to pointers. Any pointer converts implicitly to `*void`:

```anchor
var ref = &p
var ptr: *Point = ref   # ok
free(ptr)               # *Point converts to *void automatically
```

### Pointer Arithmetic

Pointers support addition, subtraction, and difference:

```anchor
extern func malloc(size: usize): *void
extern func free(ptr: *void)

var buf: *int = malloc(16) as *int
*buf = 10

var second = buf + 1
*second = 42

var diff = second - buf   # pointer difference: 1

buf += 1                  # compound assignment
buf -= 1

free(buf)
```

### Arrays

Fixed-size arrays use `T[N]` syntax:

```anchor
var arr: int[5] = [1, 2, 3, 4, 5]
var inferred = [5, 4, 3, 2, 1]

var elem = arr[2]         # indexing
var len = arr.len         # compile-time length (usize)
var ptr: *int = arr.ptr   # pointer to first element
```

### Slices

Slices (`T[]`) are fat pointers with a runtime length. Arrays convert implicitly to slices:

```anchor
var arr: int[5] = [1, 2, 3, 4, 5]
var s: int[] = arr

var elem = s[2]
var len = s.len           # runtime length (usize)
var ptr: *int = s.ptr
```

## Operators

### Arithmetic

`+`, `-`, `*`, `/` for numeric types. `^` for bitwise XOR on integers.

### Comparison

`==`, `!=`, `<`, `>`, `<=`, `>=`

### Logical

`and`, `or`, `not`

```anchor
if a > 0 and b < 10
    return 1
end
if not done
    return 0
end
```

### Compound Assignment

`+=`, `-=`, `*=`, `/=`

### Unary

`-` (negation), `&` (address-of), `*` (dereference), `not` (logical not)

### Type Cast

The `as` keyword casts between compatible types:

```anchor
var x: usize = 10
var y = x as int          # numeric cast

var buf = malloc(16) as *int    # pointer cast

var h: &Hashable = &pair
var p = h as &Pair        # interface-to-struct downcast

var c = Color.RED
var n = c as int          # enum to integer
```

### Sizeof

`sizeof(T)` returns the size of a type in bytes as `usize`:

```anchor
var s = sizeof(int)       # 4
var ps = sizeof(Point)    # size of struct
var ptr: *int = malloc(sizeof(int)) as *int
```

## Control Flow

### If / Elseif / Else

```anchor
if x > 0
    return 1
elseif x < 0
    return -1
else
    return 0
end
```

Pointers can be used as conditions (null check):

```anchor
if ptr
    return *ptr
end
```

### For Loops

Range-based loops with `in ... until` syntax. The upper bound is exclusive.

```anchor
for i in 0 until 10
    result += i
end

for i in 0 until 100 step 2
    result += i
end
```

### While Loops

```anchor
while i < 10
    i += 1
end
```

### Break and Continue

`break` exits the innermost loop or match. `continue` skips to the next iteration.

```anchor
for i in 0 until 100
    if i > 10
        break
    end
    if i == 5
        continue
    end
    result += i
end
```

### Match

Pattern matching on values:

```anchor
match x
case 0
    return 0
case 1
    return 1
case 2, 3, 4
    return 2
else
    return -1
end
```

Cases can list multiple values separated by commas. The `else` branch is optional.

## Functions

```anchor
func add(a: int, b: int): int
    return a + b
end
```

Functions with no return value omit the return type:

```anchor
func greet()
    # ...
end
```

### Export

Functions (and other top-level declarations) can be exported for use by other modules:

```anchor
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

### Extern

Declare external C functions for FFI:

```anchor
extern func malloc(size: usize): *void
extern func free(ptr: *void)
```

## Structs

Structs are value types with named fields:

```anchor
struct Point
    x: int
    y: int
end

var p = Point(x = 3, y = 7)
var a = p.x
```

Struct literals use named fields in parentheses.

### Methods

Methods are defined inside the struct body. They access fields through `self`:

```anchor
struct Pair
    a: int
    b: int

    func sum(): int
        return self.a + self.b
    end
end

var p = Pair(a = 3, b = 7)
var s = p.sum()     # 10
```

### Self-Referential Structs

Structs can reference themselves through pointers:

```anchor
struct Node
    value: int
    next: *Node
end

var n = Node(value = 42, next = null)
```

## Enums

```anchor
enum Color
    RED
    GREEN
    BLUE
end

var c = Color.RED
var c2: Color = Color.GREEN
```

Enums can be cast to/from integers (zero-indexed) and compared with `==`:

```anchor
var n = Color.GREEN as int   # 1

match c
case Color.RED
    return 0
case Color.GREEN
    return 1
case Color.BLUE
    return 2
end
```

## Interfaces

Interfaces declare method signatures. Structs implement interfaces implicitly by having matching methods.

```anchor
interface Hashable
    func hash(): int
end

struct Pair
    a: int
    b: int

    func hash(): int
        return self.a ^ self.b
    end
end
```

`Pair` satisfies `Hashable` because it has a `hash()` method with a matching signature. No explicit declaration is needed.

Interfaces are used through references:

```anchor
func get_hash(h: &Hashable): int
    return h.hash()
end

var p = Pair(a = 3, b = 9)
var h = get_hash(&p)        # pass struct ref as interface ref
```

Interface references can be downcast back to the concrete struct type:

```anchor
var h: &Hashable = &p
var p2 = h as &Pair
var val = p2.b
```

## Generics

### Generic Functions

Type parameters are declared in square brackets. They can be inferred from arguments or specified explicitly.

```anchor
func max[T](a: T, b: T): T
    if a > b
        return a
    end
    return b
end

var a = max(3, 5)          # T inferred as int
var b = max[int](7, 2)     # T specified explicitly
```

### Generic Structs

```anchor
struct Pair[K, V]
    key: K
    value: V
end

var p = Pair[int, int](key = 42, value = 10)
```

Generic structs can be self-referential:

```anchor
struct Node[T]
    value: T
    next: *Node[T]
end

var n = Node[int](value = 42, next = null)
```

### Generic Methods

Methods on structs can have their own type parameters:

```anchor
struct HeapAllocator

    func create[T](): *T
        return malloc(sizeof(T)) as *T
    end

    func destroy[T](ptr: *T)
        free(ptr)
    end
end

var heap = HeapAllocator()
var ptr = heap.create[int]()
*ptr = 42
heap.destroy[int](ptr)
```

Generic methods can also infer type parameters from arguments:

```anchor
struct Box
    value: int

    func add[T](x: T): T
        return x
    end
end

var b = Box(value = 5)
var r = b.add(7)           # T inferred as int
```

## Resource Management

The `with` statement provides scoped resource management. When the scope exits, the `release()` method is called automatically on the bound variable.

### With a New Variable

```anchor
struct Resource
    value: int

    func release()
        # cleanup logic
    end
end

with var r = Resource(value = 42)
    var x = r.value
end
# r.release() called here automatically
```

### With an Existing Variable

```anchor
var r = Resource(value = 10)
with r
    var x = r.value
end
# r.release() called here
```

### Cleanup on Early Exit

`release()` is called before `return`, `break`, or `continue` exits a `with` scope:

```anchor
func do_work(): int
    with var r = Resource(value = 42)
        return r.value    # r.release() called before return
    end
    return 0
end
```

### Nested With

Inner resources are released before outer ones:

```anchor
with var outer = Outer(value = 1)
    with var inner = Inner(value = 2)
        return inner.value + outer.value
    end
    # inner.release() called first
end
# outer.release() called second
```

## Modules and Imports

### Package Manifest

A package is defined by an `anchor` file (no extension) in the project root:

```
name mypackage
entry main
```

- `name` -- the package name, used in symbol mangling
- `entry` -- the entry module (maps to `src/main.anc`)

### Imports

Import specific symbols from other modules:

```anchor
from utils import sign
from core.math import abs
```

The module path `core.math` maps to the file `src/core/math.anc`.

### Exports

Top-level declarations can be exported with the `export` keyword:

```anchor
export const TAU = 6.28
export var counter = 0
export func add(a: int, b: int): int
    return a + b
end
export struct Point
    x: int
    y: int
end
export enum Color
    RED
    GREEN
    BLUE
end
```

Only exported symbols are visible to other modules via `import`.

## Global Variables

Variables and constants at the top level of a module are global:

```anchor
var counter = 0
const MAX = 100

func increment()
    counter += 1
end
```

Non-exported globals are module-private (static in the generated C).
