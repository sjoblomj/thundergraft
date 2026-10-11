# ThunderGraft modernization

Goal: build ThunderGraft on Linux and other modern setups using only open-source
dependencies, with miniaudio replacing FMOD Ex.

The output stays a 32-bit Windows DLL (`ThunderGraft.qdp`), because MPQDraft and
the games are Win32 programs. On Linux we cross-compile with mingw-w64 and test
under Wine.

## Decisions

- **Toolchain:** mingw-w64 (i686) + CMake.
- **Audio:** miniaudio, vendored unmodified, playing on its own output device
  instead of the game's DirectSound.
- **Formats:** WAV, MP3, Ogg Vorbis, FLAC. Opus later.
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

## Step 1: compile on modern machines and Linux

Milestone:
- `cmake --build` on Linux produces `ThunderGraft.qdp`.
- MPQDraft loads it and shows the About dialog, and the Storm hooks install.
- The test suite runs under CTest, both natively and under Wine.
- There is no audio backend yet; the hooks forward to Storm, so WAV music plays
  as it would without the plugin.

### Repo hygiene

- [ ] `.gitignore`: build directories and per-user IDE files (`.idea/workspace.xml` etc.).
- [ ] `.gitattributes` and a line-ending decision (sources are currently CRLF).
      If we normalize to LF, do it in a commit of its own.
- [ ] Delete the unused `Common/QResource.*` and `Common/QInjectDLL.*`.
- [ ] Delete `ThunderGraft.sln` and `ThunderGraft.vcproj`.
- [ ] Replace the precompiled-header setup (`StdAfx.*`) with normal includes. This
      also fixes the `#include "stdafx.h"` vs `StdAfx.h` case mismatch, which
      breaks on Linux.

### Build system

- [ ] `CMakeLists.txt`, a toolchain file `cmake/i686-w64-mingw32.cmake`, and
      `CMakePresets.json` with debug/release presets (CLion picks these up).
- [ ] Name the output `ThunderGraft.qdp` (no `lib` prefix). Debug builds define
      `_DEBUG`, which enables QDebug logging and the debug `PLUGIN_ID`.
- [ ] Compile `ThunderGraft.rc` with windres; replace `afxres.h` with `windows.h`.
- [ ] Export `GetMPQDraftPlugin` undecorated at ordinal 100, via the `.def` file
      and `-Wl,--kill-at`. Without that, GCC exports it as `GetMPQDraftPlugin@4`.
- [ ] Link the runtime statically (`-static -static-libgcc -static-libstdc++`).
      The plugin is injected into games and extracted from SEMPQs, so it can't
      depend on `libstdc++-6.dll` or similar.
- [ ] Build `-Wall -Wextra` with zero warnings in our code.
- [ ] Windows-compatibility guard: compile our sources with `_WIN32_WINNT=0x0501`.
      mingw defaults to Windows 10 headers, so APIs newer than XP otherwise slip
      in silently. For example, `GetModuleInformation` currently becomes
      `K32GetModuleInformation`, which only exists on Windows 7 and later.
- [ ] Don't use `std::thread`/`std::mutex`, because GCC's win32 implementation
      needs Vista. Keep the Win32 primitives the code already uses.
- [ ] Nice to have: the same CMake project also builds with MSVC on Windows.

### Source changes

- [ ] Replace `stdext::hash_map`, `hash_set` and the `stdext::hash_value`
      specialization with `std::unordered_map`/`unordered_set` and a hash for the
      `std::pair` key. Affects `ThunderGraft.h` and `Common/QHookAPI.*`.
- [ ] Replace the MSVC-only `__try`/`__except` in `Common/QHookAPI.cpp` with explicit
      checks of the executable headers: signatures, and addresses that stay
      within `SizeOfImage`.
- [ ] Remove the StarEdit integration:
  - `StarEdit.cpp`
  - `m_hStarEdit`, `m_dwSETimeStamp`
  - `GetStarEditVersion`, `PatchStarEditFunctions`, `GetOpenSoundFileNameA`,
    `GetSoundLengthFromIndex`
  - the StarEdit branch and its error message in `InitializePlugin`
- [ ] Remove FMOD and everything that only existed for it:
  - FMOD includes and types.
  - The `fmodex.dll` lookup in `GetModules` (which now reports 0 modules) and in
    `InitializePlugin`.
  - Delay-loading: `DelayHlpStub.cpp`, `__HrLoadAllImportsForDll2`,
    `DelayLoadHook`.
  - DirectSound device hooks: `DirectSoundCreate`/`DirectSoundCreate8` (including
    their branches in the `GetProcAddress` hook), the `IDirectSound::Release`
    reference-count handling, `CreateSoundBuffer`, the forced 44 kHz in
    `IDirectSoundBuffer::SetFormat`, and the v-table patch bookkeeping.
  - `SetDriverFromGUID`, the SigmaTel workaround, and `rpcrt4`/`UuidToString`.
- [ ] Put the audio engine behind a small internal interface: open a stream from
      a Storm handle, play/stop, volume/pan/pitch, position/length, is-playing.
      Rewrite `PlaySound`, `StopSound`, `UpdateSound` and the DDA hooks on top of
      it. Step 1 has a fake implementation for tests; until step 2, the hooks
      forward to the original Storm functions.
- [ ] Keep, ported to the new interface:
  - the Storm DDA hooks and the `GetProcAddress` hook
  - the deferred `SFileCloseFile`
  - handling of game-supplied `IDirectSoundBuffer`s. Diablo II polls
    `GetStatus` on these; see the 2011.7.25 fix.
  - focus muting and the update thread
- [ ] Keep platform-independent logic free of `<windows.h>` so it can be
      unit-tested natively. That covers the volume/pan conversions, byte↔frame
      maths, sound bookkeeping, and later the Ogg length reader.
- [ ] Clean-ups:
  - use `%p` for pointers in log calls
  - `NOMINMAX` + `std::min`/`std::max`
  - remove the dead `ShutDown()`, which is never called
- [ ] Bump `BUILD_STRING`. Keep `PLUGIN_ID` unless we find a reason to change it.

### Tests

- [ ] Test framework: doctest (single header, MIT), run through CTest.
- [ ] CTest runs the Windows test programs through Wine
      (`CMAKE_CROSSCOMPILING_EMULATOR`), plus a native Linux build of the
      platform-independent tests.
- [ ] Build-output test: the DLL exports `GetMPQDraftPlugin` at ordinal 100,
      undecorated, and imports only DLLs on a system-DLL allowlist.
- [ ] Unit tests for the DirectSound ↔ linear volume and pan conversions:
      round trips, clamping at `DSBVOLUME_MIN`/`MAX`, and the sign of pan.
- [ ] Unit tests for `PatchImportEntry`:
  - patching by name and by ordinal
  - imports already patched by another module (chained hooks)
  - malformed headers fail cleanly; this replaces the old SEH safety net
- [ ] Plugin interface tests: `Identify`, `GetPluginName`, `GetModules` (0 modules),
      and that the About dialog resource exists.
- [ ] Hook integration test, run under Wine:
  - Setup: a fake `Storm.dll` exporting the SFile/DDA functions at their real
    ordinals (0xFD–0x10F), and a fake game executable that imports them. The
    test loads the plugin and calls `GetMPQDraftPlugin` and `InitializePlugin`
    with a fake `IMPQDraftServer`.
  - Import-table entries and `GetProcAddress` lookups by ordinal resolve to the
    hooks.
  - `SFileCloseFile` on a playing handle is deferred until the sound stops.
  - Begin/End/GetPos/GetVolume/SetVolume behave correctly against the fake
    engine, including `SFILE_DDA_DEFAULT`.
  - A game-supplied buffer (a fake `IDirectSoundBuffer`) is silenced, played
    looping, and stopped when the sound ends, so `GetStatus` reports the right
    state (Diablo II dialogue).
  - The frequency correction for buffers that assume 22050 Hz.

## Step 2: miniaudio

Milestone: WAV, MP3, Ogg Vorbis and FLAC music plays in all supported games,
via MPQDraft, SEMPQs and FireGraft, on Wine and Windows.

### Integration

- [ ] Vendor miniaudio 0.11.25 and its `extras/stb_vorbis.c`, unmodified, in
      `third_party/`, with their license files.
  - Compile them through one small C file of ours, using the documented
        stb_vorbis include order.
  - Enable only the WASAPI, DirectSound and WinMM backends
        (`MA_ENABLE_ONLY_SPECIFIC_BACKENDS`, `MA_ENABLE_WASAPI`,
        `MA_ENABLE_DSOUND`, `MA_ENABLE_WINMM`).
  - Disable encoding and tone generation (`MA_NO_ENCODING`, `MA_NO_GENERATION`).
- [ ] Storm file access: a custom `ma_vfs` that opens a made-up path encoding the
      Storm handle (e.g. `sfile:<handle>`), and maps read/seek/tell/size to
      `SFileReadFile`/`SFileSetFilePointer`/`SFileGetFileSize`. The "tell"
      function matters: without it, the MP3 decoder skips detecting tags at the
      end of the file (ID3v1/APE).
- [ ] Open sounds with `MA_SOUND_FLAG_STREAM`, so Storm reads and decoding happen
      on miniaudio's background thread rather than in the audio callback. Add
      `MA_SOUND_FLAG_LOOPING` for `SFILE_DDA_LOOPING`.
- [ ] Detect the format from the content, not the file extension (the games
      hard-code `.wav` filenames).
- [ ] Create and destroy the engine on our own thread, because miniaudio
      initializes COM on whichever thread creates it. Shut it down from the
      `SFileDdaDestroy` hook, never from `DllMain`.
- [ ] If the engine fails to start, fall back to forwarding to Storm (WAV still
      plays) rather than going silent.
- [ ] Conversions:
  - volume: `ma_volume_db_to_linear(v / 100.0f)`
  - pan: `ma_pan_mode_balance` plus the existing DirectSound→linear pan conversion
  - frequency: `ma_sound_set_pitch` as a ratio, including the 22050 Hz
    correction for game-supplied buffers
- [ ] The start offset for `SFileDdaBeginEx` and the values from `SFileDdaGetPos`
      are in PCM bytes; convert using the decoded format, as FMOD did. Keep the
      rule that the reported position is always less than the length.
- [ ] Ogg Vorbis length: read the granule position of the last Ogg page through
      Storm by scanning the last ~64 KB for `OggS`, with a fallback when it isn't
      found. We need this because miniaudio reports length 0 for Vorbis unless it
      reads from a real file path or memory.
- [ ] Mute non-global sounds when the game loses focus from the update thread. A
      separate output device isn't muted automatically the way DirectSound
      buffers are.

### Tests

- [ ] Commit test audio files to `tests/fixtures/`, together with the ffmpeg script
      that generated them, so running the tests doesn't need ffmpeg:
  - MP3: CBR, VBR, a large leading ID3v2 tag (100 KB or more; FMOD gave up after
    4 KB), trailing ID3v1/APE tags, and an MP3 renamed to `.wav`
  - Ogg Vorbis, FLAC, and 8- and 16-bit WAV
  - mono and stereo; 22.05, 44.1 and 48 kHz
  - bad input: truncated files, garbage, zero-length files, a non-audio file
    named `.wav`
- [ ] Decoder tests through the Storm file layer against a fake Storm: format
      detection, exact length, number of decoded frames equals the length, seeking
      to a byte offset, and rewinding for loops.
- [ ] Ogg length reader tests:
  - matches a full decode
  - the last page straddles the scan window
  - no `OggS` found
  - truncated last page
- [ ] Engine tests with miniaudio's null backend (no audio hardware needed, so
      they run under Wine and in CI):
  - play/stop and looping
  - end detection stops the game-supplied buffer
  - volume/pan/pitch are applied
  - `GetPos` advances
  - focus muting
- [ ] Concurrency tests: hooks called from several threads while the update thread
      runs; closing a file while it plays; stopping a sound just as its stream
      ends.
- [ ] Re-run the step 1 hook integration test with the real miniaudio engine on
      the null backend.

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
- [ ] Docs:
  - a README with build and test instructions, replacing
    `Building ThunderGraft.txt`
  - `ReadMe.txt`: the new format list, no more `fmodex.dll`, StarEdit removed,
    credits for miniaudio and stb_vorbis, third-party license notices, and a
    changelog entry

## Later

- [ ] Opus, through miniaudio's `extras/decoders/libopus`:
  - libopus and libopusfile (both BSD), cross-built through CMake.
  - Length: the granule of the last page minus the pre-skip, at 48 kHz.
- [ ] Once Opus brings in libogg, consider libvorbis instead of stb_vorbis. It is
      more battle-tested and reports Vorbis length directly.
- [ ] A fake Storm backed by StormLib (MIT), to test files read from real MPQs.
- [ ] CI: build, native tests and Wine tests on every push.

## Notes from the investigation

- **miniaudio 0.11.25 builds cleanly.**
  - It compiles with i686-w64-mingw32 GCC 13 at `-Wall -Wextra` with zero warnings.
  - A statically linked test DLL imported only `KERNEL32.dll` and `msvcrt.dll`;
    miniaudio loads the audio DLLs itself at runtime.
  - The test DLL was about 660 KB stripped.
- **Decoding through read/seek callbacks:**
  - An MP3 with a 158 KB leading ID3v2 tag decoded with its exact length.
  - An MP3 renamed to `.wav` was detected by its content.
  - FLAC and WAV gave exact lengths.
  - Ogg Vorbis decoded and looped, but reported length 0. Decoding from a memory
    buffer reported the Ogg length correctly.
- **Plugin interface compatibility:** MPQDraft's plugin interface is COM-style
  (stdcall virtual methods, no virtual destructor), which GCC lays out the same
  way as MSVC. Check this early against the real, MSVC-built MPQDraft.
