# Third-party code

Vendored unmodified. Update by replacing the files with those of a newer release. Of StormLib, only `src/`, `CMakeLists.txt`, `LICENSE` and `README.md` are kept; its line endings are left as they are (see `.gitattributes`).

| Directory | What | Version | License |
|---|---|---|---|
| `doctest/` | [doctest](https://github.com/doctest/doctest) test framework (tests only) | 2.5.3 | MIT (`doctest/LICENSE.txt`) |
| `miniaudio/` | [miniaudio](https://miniaud.io) audio playback and decoding (`miniaudio.h`, `miniaudio.c`) | 0.11.25 | Public domain or MIT-0 (`miniaudio/LICENSE`) |
| `miniaudio/extras/stb_vorbis.c` | [stb_vorbis](https://github.com/nothings/stb) Ogg Vorbis decoder, as shipped with miniaudio 0.11.25 | 1.22 | Public domain or MIT (end of the file) |
| `tomlplusplus/` | [toml++](https://github.com/marzer/tomlplusplus) TOML parser, the single-header `toml.hpp`, for playlist files | 3.4.0 | MIT (`tomlplusplus/LICENSE`) |
| `StormLib/` | [StormLib](https://github.com/ladislav-zezula/StormLib) MPQ library, with its bundled copies of zlib, bzip2, LibTomCrypt, LibTomMath and the LZMA SDK (tests only) | 9.40 | MIT (`StormLib/LICENSE`). Bundled: zlib license (`src/zlib/zlib.h`); bzip2's BSD-style license, whose text StormLib leaves out; LibTomCrypt and LibTomMath "free for all purposes"; LZMA SDK and lookup3 public domain |
