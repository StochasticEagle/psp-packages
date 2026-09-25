# Package source migration TODO

All package recipes with an available Git source have been migrated to checked-out source submodules.

Packages are migrated only when there is a clear Git repository that corresponds to the package lineage. For maintained projects with releases, the parent repository gitlink identifies the release revision used by the recipe. A development-branch snapshot is appropriate only when the project has no practical release target. Package builds consume the checked-out gitlink revision and must not require branch/tag history or fetch source from the network.

## Remaining

None.

## Notes

- AngelScript is migrated to a shallow component at the current stable release.
- The SDL 1.2 and SDL2 package families are migrated to dedicated shallow components pinned to the exact revisions used by their recipes. Shared upstream repositories use separate component checkouts where different ABI/API generations require different revisions.
- Lua has been consolidated onto Lua 5.5 as `lua55`. Lua 5.5 is not ABI-compatible with Lua 5.4, so all C modules must be rebuilt. LuaSocket now depends on `lua55` but remains build-blacklisted pending Lua 5.5 compatibility testing.
- `argtable2` uses the PSP-maintained Git repository at `StochasticEagle/psp-argtable2`.
- `pixman` uses the authoritative freedesktop.org Git repository pinned to the exact 0.40.0 release commit.
- SQLite is consolidated onto the single `components/sqlite` source component; the obsolete `sqlite374` component has been removed.
- PolarSSL is no longer present as a package recipe. Mbed TLS is the maintained successor and its component is aligned to the current stable release.
- Lua 5.1/5.2/5.3/5.4 package recipes have been removed in favor of `lua55`.
