# ThunderGraft

An [MPQDraft](http://sourceforge.net/projects/mpqdraft/) plugin that lets Diablo, Diablo II, StarCraft and Warcraft II: Battle.net Edition play music in MP3, Ogg Vorbis and FLAC, as well as WAV. See [`docs/README.md`](docs/README.md) for what it does and how to use it.

The plugin is a 32-bit Windows DLL, `ThunderGraft.qdp`, as the games and MPQDraft are 32-bit Windows programs. It is built with GCC (mingw-w64), normally cross-compiled on Linux, and only depends on open-source code: [miniaudio](https://miniaud.io) and stb_vorbis, which are compiled into the plugin. See [`third_party/README.md`](third_party/README.md).

## Building

You need CMake 3.20 or later and the 32-bit mingw-w64 toolchain. On Debian or Ubuntu:

```sh
sudo apt install cmake g++-mingw-w64-i686 binutils-mingw-w64-i686
```

Then:

```sh
cmake --preset mingw-release
cmake --build --preset mingw-release
```

The plugin is `build/mingw-release/bin/ThunderGraft.qdp`. The `mingw-debug` preset builds `ThunderGraftD.qdp` instead, which writes a log to the Windows temporary directory and has a plugin ID of its own, so both can be installed side by side.

The plugin is linked statically and imports only DLLs that come with Windows, so it can be dropped into MPQDraft or an SEMPQ on its own. It is meant to load on Windows XP and later: the code is compiled against XP's API, and the build checks that the DLL imports nothing XP lacks. Only Wine and current Windows are actually tested so far.

## Testing

The tests run under [Wine](https://www.winehq.org/) when cross-compiling, in a Wine prefix of their own inside the build directory:

```sh
sudo apt install wine   # Wine has to be able to run 32-bit programs
ctest --preset mingw-debug
```

The platform-independent parts (the audio engine, the Ogg length reader, unit conversions) can also be built and tested natively, which is quicker:

```sh
cmake --preset native
cmake --build --preset native
ctest --preset native
```

What the tests cover:

| Test | What |
|---|---|
| `portable_tests` | The audio engine on miniaudio's null output device, playing every supported format from `tests/fixtures`; what the engine outputs, rendered without a device and measured, for volume, pan, pitch and resampling; the Ogg Vorbis length reader; DirectSound unit conversions |
| `dda_player_tests` | The emulation of Storm's DDA functions, against a fake engine, fake Storm file functions and fake DirectSound buffers; and on the real engine, measuring what DDA settings sound like |
| `hook_api_tests` | Import table patching, including damaged images; the Windows XP compatibility shim |
| `plugin_tests` | The built plugin loaded into a stand-in game with a fake `Storm.dll`: exports, the plugin interface, that the hooks are installed, and that without an audio engine everything is forwarded to Storm |
| `plugin_engine_tests` | The same, with the audio engine on a null output device, playing real files through the fake Storm |
| `plugin_dll_check` | The plugin's exports and imports |

The audio files in `tests/fixtures` are committed; `tests/fixtures/generate.sh` documents how they were made.

If a test case runs for more than 90 seconds, the test program says which one and exits (`TG_TEST_WATCHDOG_SECONDS` changes the limit).

## Releasing

Pushing a tag builds the release preset and runs the tests on GitHub Actions (`.github/workflows/release.yml`). If they pass, it makes a GitHub release named after the tag with `ThunderGraft-<tag>.qdp` attached. The release notes are the section of [`CHANGELOG.md`](CHANGELOG.md) whose heading is the tag, without a leading `v`; without one, GitHub lists the commits since the last release.

```sh
git tag 2026.10.12.1
git push origin 2026.10.12.1
```

The version the plugin reports to MPQDraft is `BUILD_STRING` in `src/ThunderGraft.h`, which isn't taken from the tag.

## Debugging

The `THUNDERGRAFT_AUDIO` environment variable changes how the plugin plays sounds:

- `none`: no audio engine; everything is forwarded to Storm, as if ThunderGraft weren't there
- `null`: an audio engine that plays on miniaudio's null device, which consumes the audio without playing it

Debug builds log to `ThunderGraft log - <date and time>.txt` in the Windows temporary directory.

## Layout

| Path | What |
|---|---|
| `src/` | The plugin |
| `docs/` | The user readme |
| `Common/` | Helpers that came from MPQDraft's source, since modified: the plugin interface, import table patching, debug logging |
| `tests/` | Tests, fakes, and the audio fixtures |
| `third_party/` | Vendored dependencies |
| `cmake/` | The mingw-w64 toolchain file |
| `.github/workflows/` | The release workflow |

## License

ThunderGraft is available under the Common Development and Distribution License (CDDL) 1.0. The third-party code in `third_party/` has licenses of its own, listed in `third_party/README.md`.
