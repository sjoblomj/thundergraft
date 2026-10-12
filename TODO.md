# ThunderGraft modernization

Goal: build ThunderGraft on Linux and other modern setups using only open-source
dependencies, with miniaudio replacing FMOD Ex.

The output stays a 32-bit Windows DLL (`ThunderGraft.qdp`), because MPQDraft and
the games are Win32 programs. On Linux we cross-compile with mingw-w64 and test
under Wine. Build and test instructions are in `README.md`.

## Status

Steps 1 and 2 are done: the plugin builds with mingw-w64 and CMake, plays WAV,
MP3, Ogg Vorbis and FLAC through miniaudio, and has automated tests that pass
natively and under Wine. Nothing has been tried in a real game yet.

What's left, in order of importance:

1. Try it with the real MPQDraft and games (see the manual test matrix below).
   In particular, check early that MSVC-built MPQDraft can call the GCC-built
   plugin interface.
2. CI on every push, and the other items under "Later". Tagged releases
   are built, tested and published by `.github/workflows/release.yml`.

## Decisions

- **Toolchain:** mingw-w64 (i686) + CMake.
- **Audio:** miniaudio, vendored unmodified, playing on its own output device
  instead of the game's DirectSound.
- **Formats:** WAV, MP3, Ogg Vorbis, FLAC. Opus is not planned; it can be added
  later if needed (see "Not planned" below).
  Compared with FMOD Ex we drop WMA, AIFF, MP2, VAG, tracker formats and MIDI.
- **StarEdit integration:** removed entirely. Non-WAV StarCraft map trigger
  sounds still play in-game, since they go through the same Storm streaming
  functions; only importing them in the editor goes away.
- **Ogg Vorbis length:** we read it ourselves from the last Ogg page.
- **Windows support:** current Windows and Wine are the targets. Windows 7,
  Vista and XP are best effort: don't break them needlessly, but don't bend over
  backwards for them either.
- **Licensing:** the code is CDDL 1.0, which is GPL-incompatible. Only permissively
  licensed dependencies (public domain, MIT, BSD).
- **Tests:** plenty of them.
  - Platform-independent logic: fast native tests on Linux.
  - Win32 behaviour: tests under Wine.
  - Everything else: a manual test matrix in the real games.

## Step 1: compile on modern machines and Linux (done)

### Repo hygiene

- [x] `.gitignore` for build directories and `.idea/`.
- [x] `.gitattributes`; sources normalized from CRLF to LF in a commit of their own.
- [x] Deleted the unused `Common/QResource.*` and `Common/QInjectDLL.*`.
- [x] Deleted `ThunderGraft.sln` and `ThunderGraft.vcproj`.
- [x] Replaced the precompiled-header setup (`StdAfx.*`) with normal includes.

### Build system

- [x] `CMakeLists.txt`, `cmake/i686-w64-mingw32.cmake`, and `CMakePresets.json`
      with `mingw-debug`, `mingw-release` and `native` presets.
- [x] Output named `ThunderGraft.qdp` (`ThunderGraftD.qdp` for debug builds,
      which define `_DEBUG`).
- [x] `ThunderGraft.rc` compiled with windres; `afxres.h` replaced with `windows.h`.
- [x] `GetMPQDraftPlugin` exported undecorated at ordinal 100. Besides
      `--kill-at`, this needed C linkage on its declaration in `MPQDraftPlugin.h`.
- [x] Runtime linked statically.
- [x] Zero warnings at `-Wall -Wextra`.
- [x] Code that goes into the plugin compiles with `_WIN32_WINNT=0x0501`. Test
      programs don't, as they use `std::thread`, which needs Vista with GCC.
- [x] No `std::thread`/`std::mutex` in plugin code.
- Building with MSVC: not planned; see "Not planned" below.

### Source changes

- [x] `stdext` hash containers replaced with `std::unordered_map`/`unordered_set`.
- [x] `__try`/`__except` in `QHookAPI.cpp` replaced with explicit bounds checks
      on the PE headers, import descriptors, thunk tables and names. Also fixed:
      imported modules that aren't loaded were treated as the export module.
- [x] StarEdit integration removed.
- [x] FMOD removed, with delay-loading, the DirectSound device hooks, the forced
      44 kHz, and the RPC dependency.
- [x] Audio engine behind `IAudioEngine` (`AudioEngine.h`); the DDA emulation
      moved into `CDdaPlayer`.
- [x] Kept the DDA and `GetProcAddress` hooks, the deferred `SFileCloseFile`,
      game-supplied `IDirectSoundBuffer` handling, focus muting and the update
      thread.
- [x] Conversions in `DdaConversions.h`, free of Windows headers.
- [x] Clean-ups: `%p` in logs, `NOMINMAX`, dead code removed (`ShutDown()`, the
      disabled `LoadLibraryA` hook). `QDebug` bugs fixed on the way: inverted
      handle checks in the close functions, unbounded `vsprintf`.
- [x] `BUILD_STRING` comes from the `THUNDERGRAFT_VERSION` CMake variable, which
      the release workflow sets from the tag. Keep `PLUGIN_ID` unless we find a
      reason to change it.

Behaviour changes in the DDA emulation, deliberate:
- `SFileDdaSetVolume` succeeds on finished sounds, as the original comments say
  Storm does, and also updates a game-supplied buffer so the update thread
  doesn't undo the change.
- Sounds started while the game is in the background start muted.
- The last partial read at the end of a file is no longer dropped.

### Tests

- [x] doctest (vendored), run through CTest; Windows test programs run under Wine
      in a prefix of their own in the build directory.
- [x] `plugin_dll_check`: exports, DLL allowlist, and (added later) no kernel32
      functions that XP lacks.
- [x] Conversion unit tests. Only DirectSound → linear: the reverse conversions
      turned out to be unneeded and were dropped.
- [x] `PatchImportEntry` tests: by name, by ordinal, recursive, chained, and
      hand-made damaged images placed before an inaccessible page.
- [x] Plugin interface tests.
- [x] Hook integration test with a fake `Storm.dll` and a stand-in game.
- [x] `CDdaPlayer` tests against a fake engine, fake Storm and fake DirectSound
      buffers, including the Diablo II `GetStatus` case and concurrent use.

## Step 2: miniaudio (done)

### Integration

- [x] miniaudio 0.11.25 and its stb_vorbis vendored unmodified, compiled through
      `MiniaudioImpl.c`. Backends: WASAPI, DirectSound, WinMM, and null (for tests).
- [x] Storm file access through a custom `ma_vfs`. It maps made-up paths,
      `thundergraft:<id>`, to `IAudioFile`s rather than Storm handles, so the
      engine doesn't know about Storm and can be tested without it.
- [x] Sounds open with `MA_SOUND_FLAG_STREAM`, and `MA_SOUND_FLAG_LOOPING` for
      `SFILE_DDA_LOOPING`.
- [x] Format detected from the content.
- [x] Engine created on a thread of its own (COM). **Changed from the plan:**
  - It starts in the `SFileDdaInitialize` hook rather than `InitializePlugin`,
    since a patcher might call `InitializePlugin` while holding the loader lock,
    where waiting for a thread deadlocks.
  - It is never destroyed. `SFileDdaDestroy` stops all sounds instead, which also
    copes with games that initialize DDA more than once.
- [x] Falls back to forwarding to Storm if the engine fails to start.
- [x] Conversions as planned, using our own `DdaVolumeToLinear` rather than
      `ma_volume_db_to_linear`.
- [x] Positions in PCM bytes; position always less than length. **Found on the
      way:** miniaudio's default resource manager converts every sound to the
      device's sampling rate while decoding, which would have put positions in
      the wrong units. The engine has its own resource manager that keeps each
      file's rate and channels.
- [x] Ogg Vorbis length from the last page's granule position, with CRC-checked
      page search over the last 128 KB. Looping streams' cursors wrap on it.
- [x] Focus muting from the update thread.
- [x] **Not in the plan:** the GCC runtime imports `GetThreadId`, which XP lacks.
      `XpCompat.cpp` defines that import itself, with an `NtQueryInformationThread`
      fallback.

### Tests

- [x] Fixtures in `tests/fixtures/`, made by `generate.sh`: MP3 (CBR, VBR,
      120 KB ID3v2 tag, ID3v1 trailer, truncated), Ogg Vorbis, FLAC (16 and
      24-bit), WAV (8, 16 and 24-bit), mono and stereo, 22.05–96 kHz, and
      non-audio files. Not included, as names never reach the decoder: an MP3
      named `.wav`. Not included: APE tags.
- [x] Engine tests on the null device: every format opens with the right format
      and exact length, plays to its end, loops, seeks, several sounds at once,
      opening from several threads, rejection of non-audio files.
- [x] Ogg length reader tests: real files, cut-off and corrupt last pages, other
      streams' pages, pages without a granule, "OggS" inside other data, nothing
      within the search window.
- [x] End-to-end plugin tests with the engine on the null device, playing real
      files through the fake `Storm.dll`.
- [x] Output tests: an engine mode with no device renders the mix on demand, and
      the tests measure level and frequency per channel of the 440 hz fixtures.
      They cover every format decoding to the right tone for the right length,
      resampling, volume, DirectSound-style pan, pitch, muting, seeking and
      mixing, both on the engine and through `CDdaPlayer` (DDA volumes and pans
      in 1/100 dB, start positions in bytes, game-supplied buffers, focus).
      Deliberately broken volume, pan and pitch code fails them.
- [ ] **Intermittent hang, not yet diagnosed.** Three test programs (native and
      under Wine, including `plugin_engine_tests`, where the engine is never
      shut down) each hung once, all within the same few minutes; hundreds of
      runs before and since were clean, including under parallel load and under
      gdb. The test runner now has a watchdog that names the hung test case
      after 90 s; if it happens again, start from there.

## Release

- [ ] Manual test matrix:
  - Games: StarCraft/Brood War, Diablo, Diablo II/LoD, Warcraft II BNE.
  - Launch methods: MPQDraft GUI, SEMPQ, FireGraft.
  - Platforms: Wine and Windows 10/11; Windows 7 and XP best effort.
  - What to check:
    - global music with hard-coded `.wav` names
    - looping
    - Diablo II area crossfades
    - Diablo II dialogue text with "audio and text"
    - StarCraft non-WAV map trigger sounds
    - in-game volume sliders
    - muting on alt-tab
    - clean exit, with no crash (cf. the 2009.9.22 fix)
    - running alongside other plugins
- [x] Docs: `README.md` with build and test instructions; the user readme
      (now `docs/README.md`) updated
      (formats, no `fmodex.dll`, StarEdit removed, credits, licenses); the
      update history moved to `CHANGELOG.md`.
- [x] Release workflow: pushing a tag builds, tests, and publishes a GitHub
      release with `ThunderGraft-<tag>.qdp`.

## Later

- [x] Storm file functions backed by StormLib 9.40 (MIT, vendored, tests only),
      to test files read from real MPQs:
  - `tests/fakes/MpqStorm.*` wraps StormLib, locked for use from several threads,
    and failing on unknown handles rather than crashing as StormLib does.
  - `dda_player_tests` plays every format from MPQs made during the test, stored
    uncompressed, imploded, PKWARE, zlib and bzip2 compressed, encrypted (with and
    without the adjusted key), as a single unit, and with sector checksums, plus
    a WAV compressed with ADPCM and Huffman coding; and measures the output.
  - The fake `Storm.dll` now uses StormLib for its file functions, and also
    exports `SFileOpenArchive`, `SFileOpenFileEx` and `SFileCloseArchive` at
    Storm's ordinals, so `plugin_tests` opens files the way a game does.
  - Not done: tests against the games' own MPQs, which can't be committed. They
    could be run on request, with the archives' paths in an environment variable.
- [ ] CI: build, native tests and Wine tests on every push.

## Not planned

Opus support, decided against for now. If it's ever wanted:
- miniaudio's `extras/decoders/libopus` decoder, registered through the resource
  manager's `ppCustomDecodingBackendVTables` in `MiniaudioEngine.cpp`.
- libogg, libopus and libopusfile (all BSD), cross-built as static libraries;
  opusfile with `OP_DISABLE_HTTP`. opusfile looks its dependencies up with
  `find_package()`, which needs `FetchContent` package overrides or wrapper
  targets when building all three in one project.
- The length comes from opusfile (`op_pcm_total`, through seekable callbacks),
  so `OggLength.cpp` isn't involved. Opus always decodes at 48 kHz.
- With libogg in, replacing stb_vorbis with libvorbis becomes cheap.

Building with MSVC (Visual C++), decided against: it's a proprietary
toolchain, and Windows developers can build with mingw-w64 through MSYS2
using the same CMake presets. Revisit only if the real MPQDraft turns out
to have an interface problem with the GCC-built plugin that can't be fixed
on the GCC side. It would need MSVC equivalents for:
- the GCC linker flags (static runtime, `--kill-at`, stdcall fixup) and
  `-Wall -Wextra`
- `XpCompat.cpp`, which replaces the `GetThreadId` import with a GCC-only
  `__asm__` symbol name
- the test helper DLLs' import libraries, made with `dlltool`
- the `.def` export entry, written for GCC's name decoration
