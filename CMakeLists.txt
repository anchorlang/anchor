cmake_minimum_required (VERSION 3.20)

project (ancc C)

set (CMAKE_C_STANDARD 99)
set (CMAKE_C_STANDARD_REQUIRED ON)
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable (ancc
    "src/arena.c"
    "src/error.c"
    "src/fs.c"
    "src/lexer.c"
    "src/main.c"
    "src/module.c"
    "src/package.c"
    "src/parser.c"
    "src/sema.c"
    "src/type.c"
)
