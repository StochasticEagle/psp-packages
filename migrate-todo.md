# Package source migration TODO

These package recipes are not yet migrated to checked-out source submodules.

Packages are migrated only when there is a clear authoritative Git repository that corresponds to the package lineage. Migrated repositories track their upstream development branch rather than a release tag; remaining entries below either have ambiguous/non-Git provenance or are intentionally deferred as part of a compatibility migration.

## Remaining

- angelscript
- argtable2
- lua54
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
- sqlite

## Notes

- SDL 1.2/SDL2-family migration is intentionally deferred so older packages are not silently pointed at SDL3 default branches. SDL3 and its SDL3 extension packages track their authoritative `main` branches.
- `lua54` remains on the Lua 5.4 source line; the available development Git mirror is not used because moving its default branch would implicitly migrate the package to Lua 5.5.
- `mpg123` uses Subversion as its authoritative development repository; a Git mirror is not treated as authoritative.
- `sqlite` uses Fossil as its authoritative VCS; a Git mirror is not treated as authoritative.
- `polarssl` is obsolete and should be evaluated for removal/replacement by mbedTLS rather than automatically migrated.
- Lua 5.1/5.2/5.3 packages were removed. LuaSocket now targets `lua54`, but remains build-blacklisted pending compatibility testing.
