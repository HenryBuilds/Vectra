# Copyright 2026 Vectra Contributors. Apache-2.0.
#
# Stub Config file that satisfies callers of `find_package(SQLite3)`
# by delegating to the `unofficial-sqlite3` package vcpkg actually
# ships. Only needed because some vcpkg sqlite3 port versions ship a
# `share/sqlite3/vcpkg-cmake-wrapper.cmake` that calls
# `_find_package(SQLite3 CONFIG)` and expects a real
# `SQLite3Config.cmake` to be on the prefix path — but the same port
# does not actually install one. The wrapper then fails the configure
# step on Linux CI runners (Windows ships an older port without the
# wrapper, so this issue only fires there).
#
# Both the standard `SQLite::SQLite3` IMPORTED target and the vcpkg
# `unofficial::sqlite3::sqlite3` target are provided so any consumer
# can pick whichever name it knows. We do not duplicate the library —
# both targets resolve to the same underlying static / shared lib.

if(NOT TARGET unofficial::sqlite3::sqlite3)
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
endif()

if(NOT TARGET SQLite::SQLite3)
    add_library(SQLite::SQLite3 INTERFACE IMPORTED)
    target_link_libraries(SQLite::SQLite3 INTERFACE unofficial::sqlite3::sqlite3)
endif()

set(SQLite3_FOUND TRUE)
set(SQLite3_VERSION "3.53.0" CACHE STRING "stub SQLite3 version")
