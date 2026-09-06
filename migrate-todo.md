# Package source migration TODO

These package recipes are not yet migrated to checked-out source submodules.

Packages are migrated only when there is a clear authoritative Git repository that corresponds to the package lineage. Migrated repositories track their upstream development branch rather than a release tag; remaining entries below either have ambiguous/non-Git provenance, require special handling, or are intentionally deferred as part of a larger compatibility migration.

## Remaining

- angelscript
- argtable2
- freetype2
- libeigen
- liblzma
- libmecore
- lua54
- luasocket
- minizip
- mpg123
- pixman
- polarssl
- pspla
- sdl-gfx
- sdl-ttf
- sdl2-gfx
- sdl2-image
- sdl2-mixer
- sdl2-net
- sdl2-ttf
- sdl2
- sdl3-image
- sdl3-mixer
- sdl3-ttf
- sdl3
- smath
- sqlite

## Notes

- SDL-family migration is intentionally deferred so SDL 1.2/SDL2 packages are not silently pointed at SDL3 default branches. Consolidate these as a separate compatibility/API migration.
- `sqlite` uses Fossil as its authoritative VCS, so a Git mirror is not treated as authoritative.
- `polarssl` is obsolete and should be evaluated for removal/replacement by mbedTLS rather than automatically migrated.
- `lua54` remains until an authoritative Git source policy is established; Lua 5.1/5.2/5.3 packages were removed.
