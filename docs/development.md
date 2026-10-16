# Developing ThunderGraft

How ThunderGraft is built, tested and released. For what it does and how to use it, see the [user readme](README.md); for how it works inside, its section [How It Works](README.md#how-it-works-technical).

The plugin is a 32-bit Windows DLL, `ThunderGraft.qdp`, as the games and MPQDraft are 32-bit Windows programs. It is built with GCC (mingw-w64), normally cross-compiled on Linux, and only depends on open-source code: [miniaudio](https://miniaud.io), stb_vorbis and [toml++](https://github.com/marzer/tomlplusplus), which are compiled into the plugin. See [`third_party/README.md`](../third_party/README.md).

Alongside the plugin is `tgplaylist`, a command-line tool for modders that checks, converts and simulates optional playlist files ([`playlists.md`](playlists.md)), using the same code as the plugin. It is built for Windows and natively.

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

The plugin is `build/mingw-release/bin/ThunderGraft.qdp`, and the tool `build/mingw-release/bin/tgplaylist.exe`. The `mingw-debug` preset builds the same files, unoptimized, with debugging symbols and with assertions on, for running under a debugger. To see what the plugin does as a game runs, turn on debug logs in its [settings](README.md#settings), in either build.

The plugin is linked statically and imports only DLLs that come with Windows, so it can be dropped into MPQDraft or an SEMPQ on its own. It is meant to load on Windows XP and later: the code is compiled against XP's API, and the build checks that the DLL imports nothing XP lacks. Only Wine and current Windows are actually tested so far.

## Testing

The tests run under [Wine](https://www.winehq.org/) when cross-compiling, in a Wine prefix of their own inside the build directory:

```sh
sudo apt install wine   # Wine has to be able to run 32-bit programs
ctest --preset mingw-debug
```

The platform-independent parts (the audio engine, the Ogg length reader, unit conversions, the playlists, and the tool) can also be built and tested natively, which is quicker:

```sh
cmake --preset native
cmake --build --preset native
ctest --preset native
```

What the tests cover:

| Test | What |
|---|---|
| `portable_tests` | The audio engine on miniaudio's null output device, playing every supported format from `tests/fixtures`; what the engine outputs, rendered without a device and measured, for volume, pan, pitch, resampling, fades, and sounds started after one another without a gap; playlists on that engine (gapless track changes, crossfades, fade-ins); the Ogg Vorbis length reader; DirectSound unit conversions |
| `playlist_tests` | Playlists: file names and patterns, the playlist file reader (including hostile and randomly damaged files), the converter for older `playlist.txt` files, finding the base file, the order tracks play in, the player against a fake engine (transitions, the current playlist, cues, `back`, fades, crossfades, gapless changes, missing tracks, maps' playlist files), and the simulator behind `tgplaylist simulate` |
| `tgplaylist_*` | The `tgplaylist` tool, run on the files in `tests/playlists`, checking its output and exit status |
| `dda_player_tests` | The emulation of Storm's DDA functions, against a fake engine, fake Storm file functions and fake DirectSound buffers, also with playlists in place of the game's files; and on the real engine, measuring what DDA settings sound like, and what every format sounds like read from MPQs, through [StormLib](https://github.com/ladislav-zezula/StormLib), stored each way MPQs store files |
| `settings_tests` | The settings: where the debug log goes, which file names are accepted, and the file they go to the game in |
| `hook_api_tests` | Import table patching, including damaged images; finding exports by ordinal, including damaged export tables; the Windows XP compatibility shim |
| `plugin_tests` | The built plugin loaded into a stand-in game with a fake `Storm.dll`: exports, the plugin interface, that the hooks are installed, that the hooks on opening files pass results and errors through and chain with other plugins' hooks before and after ThunderGraft's (one of them redirecting opens as MPQDraft does), and that without an audio engine everything is forwarded to Storm |
| `plugin_settings_tests` | The same, with settings remembered in the registry as the Configure dialog leaves them: the plugin hands them to MPQDraft as a module, and with that module in the game, writes the debug log |
| `plugin_engine_tests` | The same, with the audio engine on a null output device, playing real files from disk and from an MPQ through the fake Storm, whose file functions are StormLib's; and playlists: a base playlist file in an MPQ, a cue and `back`, a map's playlist file while its MPQ is open, and the transitions in the debug log |
| `plugin_playlist_error_tests` | The same, with a base playlist file that has an error: the game's music plays, and `ThunderGraft.log` says why |
| `plugin_late_playlist_tests` | The same, with the base playlist file in an MPQ opened after DDA is initialized, outside the game's imports, as MPQDraft does |
| `plugin_dll_check` | The plugin's exports and imports |

The audio files in `tests/fixtures` are committed; `tests/fixtures/generate.sh` documents how they were made. The MPQs are made from them while the tests run, in the temporary directory.

If a test case runs for more than 90 seconds, the test program says which one and exits (`TG_TEST_WATCHDOG_SECONDS` changes the limit).

## Releasing

Pushing a tag builds the release preset and runs the tests on GitHub Actions (`.github/workflows/release.yml`). If they pass, it makes a GitHub release named after the tag with `ThunderGraft-<tag>.qdp` and `tgplaylist-<tag>.exe` attached. The release notes are the section of [`CHANGELOG.md`](../CHANGELOG.md) whose heading is the tag, without a leading `v`; without one, GitHub lists the commits since the last release.

```sh
git tag 2026.10.12.1
git push origin 2026.10.12.1
```

The plugin reports its version to MPQDraft as "ThunderGraft v<version>". The workflow sets it from the tag, without a leading `v`, through the `THUNDERGRAFT_VERSION` CMake variable; builds without it call themselves "ThunderGraft development build". The version is also in the `.qdp`'s Windows version resource (Properties → Details); its four-number file version comes from the leading numbers of the version, and is 0.0.0.0 when there are none. To build a release locally, pass it yourself: `cmake --preset mingw-release -DTHUNDERGRAFT_VERSION=2026.10.12.1`.

## Debugging

The `THUNDERGRAFT_AUDIO` environment variable changes how the plugin plays sounds:

- `none`: no audio engine; everything is forwarded to Storm, as if ThunderGraft weren't there
- `null`: an audio engine that plays on miniaudio's null device, which consumes the audio without playing it

Debug logs, turned on in the plugin's [settings](README.md#settings), write what the plugin does to a file of your choice, including every playlist transition and the problems with playlist files that otherwise go to `ThunderGraft.log`.

## Layout

| Path | What |
|---|---|
| `src/` | The plugin; the files not named after Windows things are platform-independent |
| `tools/` | The `tgplaylist` tool |
| `docs/` | The user readme, the playlist guide, the playlists' design, and this file |
| `Common/` | Helpers that came from MPQDraft's source, since modified: the plugin interface, import table patching, debug logging |
| `tests/` | Tests, fakes, the audio fixtures, and playlist files for the tool's tests |
| `third_party/` | Vendored dependencies |
| `cmake/` | The mingw-w64 toolchain file |
| `.github/workflows/` | The release workflow |
