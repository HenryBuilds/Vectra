# TreeSitterGrammar.cmake
#
# CMake helper for compiling a tree-sitter grammar submodule as a static
# library. Vectra registers languages via languages.toml, which lists
# each grammar's directory and entry-symbol name; this function turns
# such an entry into a buildable target.
#
# Usage:
#
#   include(TreeSitterGrammar)
#
#   add_tree_sitter_grammar(
#       NAME    rust
#       PATH    ${CMAKE_SOURCE_DIR}/third_party/grammars/tree-sitter-rust
#   )
#
# Produces:
#
#   - A static library target  ts_grammar_<name>
#   - A namespaced alias        vectra::grammar::<name>
#
# Both export include directories pointing at the grammar's src/
# directory so callers can #include <tree_sitter/parser.h> and resolve
# tree_sitter_<name>() at link time.
#
# Conventions for tree-sitter grammar repositories:
#
#   src/parser.c              required, generated from grammar.js
#   src/scanner.c             optional, hand-written external scanner
#   src/scanner.cc            optional, C++ external scanner (rare)
#   src/tree_sitter/parser.h  bundled API header
#
# We intentionally do NOT compile from grammar.js. The pre-generated
# parser.c that ships in each grammar repo is what the official
# tree-sitter project uses, and matches what Helix / nvim-treesitter
# / Zed all consume. Regenerating would require tree-sitter-cli (a
# Node dependency) and would not improve correctness.

function(add_tree_sitter_grammar)
    set(options)
    set(oneValueArgs   NAME PATH)
    set(multiValueArgs)
    cmake_parse_arguments(VTSG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT VTSG_NAME)
        message(FATAL_ERROR "add_tree_sitter_grammar: NAME is required")
    endif()
    if(NOT VTSG_PATH)
        message(FATAL_ERROR "add_tree_sitter_grammar: PATH is required")
    endif()

    set(grammar_src "${VTSG_PATH}/src")
    set(parser_c    "${grammar_src}/parser.c")

    if(NOT EXISTS "${parser_c}")
        message(FATAL_ERROR
            "add_tree_sitter_grammar(${VTSG_NAME}): parser.c not found at\n"
            "  ${parser_c}\n"
            "Did you run 'git submodule update --init --recursive'?")
    endif()

    set(target  "ts_grammar_${VTSG_NAME}")
    set(sources "${parser_c}")

    # Optional external scanners. A grammar has at most one of these.
    set(scanner_c  "${grammar_src}/scanner.c")
    set(scanner_cc "${grammar_src}/scanner.cc")
    if(EXISTS "${scanner_c}")
        list(APPEND sources "${scanner_c}")
    endif()
    if(EXISTS "${scanner_cc}")
        list(APPEND sources "${scanner_cc}")
        # The presence of a .cc scanner forces C++ on this target. CMake
        # infers this from the source extension, but we set the language
        # explicitly so future readers don't have to guess.
        set_source_files_properties("${scanner_cc}" PROPERTIES LANGUAGE CXX)
    endif()

    add_library(${target} STATIC ${sources})
    add_library(vectra::grammar::${VTSG_NAME} ALIAS ${target})

    target_include_directories(${target} PUBLIC "${grammar_src}")

    # Generated parsers are large machine output and not subject to our
    # project warning policy — silencing here keeps the rest of the
    # tree under -Werror without false positives from generated code.
    set_target_properties(${target} PROPERTIES
        C_STANDARD                  11
        C_VISIBILITY_PRESET         hidden
        CXX_VISIBILITY_PRESET       hidden
        POSITION_INDEPENDENT_CODE   ON
        FOLDER                      "third_party/grammars")

    if(MSVC)
        # /W0 disables all warnings only on this target. The /wd flags
        # cover the few that survive at /W0 in some MSVC versions.
        target_compile_options(${target} PRIVATE
            /W0
            /wd4244 /wd4267 /wd4018 /wd4101)
    else()
        target_compile_options(${target} PRIVATE
            -w   # blanket "no warnings" for generated code
            -Wno-unused-but-set-variable
            -Wno-unused-parameter
            -Wno-unused-variable
            -Wno-sign-compare)
    endif()
endfunction()
