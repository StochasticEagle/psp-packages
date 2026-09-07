# Package source migration TODO

These package recipes are not yet migrated to checked-out source submodules.

Packages are migrated only when there is a clear authoritative Git repository that corresponds to the package lineage. For maintained projects with releases, the parent repository gitlink should identify the current stable release. A development-branch snapshot is appropriate only when the project has no practical release target. Package builds consume the checked-out gitlink revision and must not require branch/tag history or fetch source from the network.

## Remaining

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

- AngelScript is migrated to a shallow component at the current stable release.
- SDL 1.2/SDL2-family migration is intentionally deferred so older packages are not silently pointed at SDL3 source. SDL3 and its extension packages must remain within their corresponding ABI/API family.
- Lua has been consolidated onto Lua 5.5 as `lua55`. Lua 5.5 is not ABI-compatible with Lua 5.4, so all C modules must be rebuilt. LuaSocket now depends on `lua55` but remains build-blacklisted pending Lua 5.5 compatibility testing.
- `mpg123` uses Subversion as its authoritative development repository; the package therefore uses the current release archive instead of an unofficial Git mirror.
- SQLite has a current-source component, but the packaged PSP VFS is a substantial legacy port. The 3.7.4 package therefore uses the dedicated shallow `sqlite374` component until that VFS is deliberately forward-ported and validated.
- `polarssl` is obsolete. Mbed TLS is the maintained successor and its component is aligned to the current stable release. PolarSSL remains temporarily for compatibility until dependent packages are moved.
- Lua 5.1/5.2/5.3/5.4 package recipes have been removed in favor of `lua55`.
