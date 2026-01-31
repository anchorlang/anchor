# Tooling

## Compiler Commands

### Run a Single File

Compile and execute a single `.anc` file:

```sh
ancc run file.anc
```

The file must contain a `func main(): int` entry point. The process exit code is the return value of `main`.

### Build a Package

Build a multi-file project from a directory containing an `anchor` manifest:

```sh
ancc build path/to/project
```

If no path is given, the current directory is used.

### Debug: Print Tokens

```sh
ancc lexer file.anc
```

### Debug: Print AST

```sh
ancc ast file.anc
```

## Project Structure

A package project has this layout:

```
myproject/
    anchor              # manifest file
    src/
        main.anc        # entry module
        utils.anc       # other modules
        core/
            math.anc    # nested module (imported as core.math)
```

### Manifest Format

The `anchor` file is plain text with key-value pairs:

```
name myproject
entry main
```

| Key     | Description                                      |
|---------|--------------------------------------------------|
| `name`  | Package name, used in generated C symbol names   |
| `entry` | Entry module name (maps to `src/<entry>.anc`)    |

## Generated Output

The compiler generates C99 source files and invokes `gcc -std=c99` to compile them.

For a package named `basic` with modules `main`, `utils`, and `core.math`, the generated files are:

```
build/
    anc__basic__main.c
    anc__basic__main.h
    anc__basic__utils.c
    anc__basic__utils.h
    anc__basic__core_math.c
    anc__basic__core_math.h
```

Symbol names are mangled as `anc__{package}__{module}__{identifier}`. Methods include the type name: `anc__{package}__{module}__{Type}__{method}`.
