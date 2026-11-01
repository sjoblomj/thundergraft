# Third-party code

Vendored unmodified. Update by replacing the files with those of a newer release. stb_vorbis is the exception: it is taken from the `dev` branch of stb rather than the version miniaudio ships, as 1.22 can corrupt the heap and leak memory when opening a damaged Ogg file, which a map can supply. Go back to a release of stb_vorbis once one has those fixes (upstream commits `7776735e` and `4111a9ec`). Of StormLib, only `src/`, `CMakeLists.txt`, `LICENSE` and `README.md` are kept; its line endings are left as they are (see `.gitattributes`).

| Directory | What | Version | License |
|---|---|---|---|
| `doctest/` | [doctest](https://github.com/doctest/doctest) test framework (tests only) | 2.5.3 | MIT (`doctest/LICENSE.txt`) |
| `miniaudio/` | [miniaudio](https://miniaud.io) audio playback and decoding (`miniaudio.h`, `miniaudio.c`) | 0.11.26 | Public domain or MIT-0 (`miniaudio/LICENSE`) |
| `miniaudio/extras/stb_vorbis.c` | [stb_vorbis](https://github.com/nothings/stb) Ogg Vorbis decoder, not the copy shipped with miniaudio | `dev` branch at `a5b6c87f` (calls itself 1.22) | Public domain or MIT (end of the file) |
| `tomlplusplus/` | [toml++](https://github.com/marzer/tomlplusplus) TOML parser, the single-header `toml.hpp`, for playlist files | 3.4.0 | MIT (`tomlplusplus/LICENSE`) |
| `StormLib/` | [StormLib](https://github.com/ladislav-zezula/StormLib) MPQ library, with its bundled copies of zlib, bzip2, LibTomCrypt, LibTomMath and the LZMA SDK (tests, and the `tgplaylist` tool; not the plugin) | 9.40 | MIT (`StormLib/LICENSE`). Bundled: zlib license (`src/zlib/zlib.h`); bzip2's BSD-style license, whose text StormLib leaves out; LibTomCrypt and LibTomMath "free for all purposes"; LZMA SDK and lookup3 public domain |
