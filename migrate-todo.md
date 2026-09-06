# Package source migration TODO

These package recipes are not yet migrated to checked-out source submodules.

Packages are migrated only when there is a clear authoritative Git repository that corresponds to the package lineage. Migrated repositories track their upstream development branch rather than a release tag; remaining entries below either have ambiguous/non-Git provenance or are intentionally deferred as part of a compatibility migration.

## Remaining

- angelscript
- argtable2
- mpg123
- pixman
- polarssl
- sdl-gfx
- sdl-ttf
- sdl2-gfx
- sdl2-image
- sdl2-mixer
- sdl2-net
- sdl2-ttf
- sdl2

## Notes

- SDL 1.2/SDL2-family migration is intentionally deferred so older packages are not silently pointed at SDL3 default branches. SDL3 and its SDL3 extension packages track their authoritative `main` branches.
- Lua has been consolidated onto Lua 5.5 as `lua55`, tracking the Lua team's `lua/lua` development mirror on `master`. Lua 5.5 is not ABI-compatible with Lua 5.4, so all C modules must be rebuilt. LuaSocket now depends on `lua55` but remains build-blacklisted pending Lua 5.5 compatibility testing.
- `mpg123` uses Subversion as its authoritative development repository. The GitHub mirrors located so far explicitly state that they are unofficial, so no Git component is used yet.
- SQLite now tracks the official read-only `sqlite/sqlite` GitHub mirror on `master`.
- `polarssl` is obsolete. The existing `components/mbedtls` submodule tracks Mbed TLS development and should be used as the migration target over time; PolarSSL remains temporarily for compatibility until dependent packages are moved.
- Lua 5.1/5.2/5.3/5.4 package recipes have been removed in favor of `lua55`.
