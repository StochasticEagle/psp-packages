# Package source migration TODO

These package recipes are not yet migrated to checked-out source submodules.

The migration pass only converted sources where the existing PSPBUILD already identified a Git repository directly (`git+...`) with an exact commit or tag. The entries below currently use release archives, non-Git downloads, or otherwise require verification of the authoritative Git repository and the exact revision corresponding to the packaged source before a gitlink can be created safely.

- angelscript
- argtable2
- cereal
- cjson
- curl
- dumb
- enet
- expat
- faudio
- flac
- flatbuffers
- fmt
- freetype2
- googletest
- harfbuzz
- jpeg
- leptonica
- libconfuse
- libeigen
- liblzma
- libmecore
- libogg
- libpng
- libvorbis
- libxmp-lite
- libxmp
- libyaml
- libzip
- lua51
- lua52
- lua53
- lua54
- luasocket
- lz4
- mbedtls
- minizip
- mpg123
- openal
- opus
- opusfile
- physfs
- pixman
- pocketpy
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
- spdlog
- sqlite
- tinyxml2
- unarr
- wolfssl
- xxhash
- zlib
- zziplib

For each item, verify the Git repository from the source already named by the PSPBUILD, identify the exact commit corresponding to the current packaged archive/version, then add it under `components/` as a shallow submodule. Do not substitute an unrelated mirror merely because one exists.
