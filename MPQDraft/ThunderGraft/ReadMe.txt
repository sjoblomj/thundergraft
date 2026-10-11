THUNDERGRAFT

What It Is
----------

ThunderGraft is an MPQDraft plugin that adds additional music-playing capabilities to Diablo, Diablo II, StarCraft, and Warcraft II: Battle.net Edition. In addition to PCM WAV audio files, which are supported natively by these games (either used uncompressed or compressed via MPQ WAV compression, which is based on lossy ADPCM compression), ThunderGraft adds the ability to play newer, superior audio compression formats: MPEG Layer-3 Audio (MP3), Ogg Vorbis (Ogg), and Free Lossless Audio Codec (FLAC). All of these give noticeably higher audio quality and (with the exception of FLAC) significantly smaller file sizes than MPQ WAV compression. ThunderGraft uses miniaudio for decoding and playing audio.

The supported games set the sound card to output 22 khz audio, as that is what their sound effects and music use. ThunderGraft plays music on an output device of its own, at the music's own sampling rate, so higher quality music is not held back by the games' settings.

ThunderGraft is game- and version-independent. ThunderGraft is limited, however, to music and other streamed sounds, including StarCraft map trigger sounds; it does not work with sound effects that are not streamed.

How to Use It
-------------

ThunderGraft is easy to use. Simply put the music files in an MPQ with WinMPQ or some other MPQ file editor (as with WAV files prior to ThunderGraft), then load the MPQ with MPQDraft and the ThunderGraft plugin; do NOT use WAV compression when importing non-PCM WAV files into an MPQ. Note that global music files, such as the opening themes of the various games, have hard-coded paths that require the audio files have a WAV extension; ThunderGraft does not rely on extension, and will play them correctly even if they are not actually WAV files.

ThunderGraft is a single file, ThunderGraft.qdp, with no other files it depends on.

Known Bugs/Side Effects
-----------------------

None known.

How It Works (Technical)
------------------------

ThunderGraft targets the Storm DDA system (SFileDda). This system handles streaming sounds from archives and disc files. This system supports only PCM WAV audio files; StarCraft added support for lossy ADPCM compression by adding ADPCM compression as a fundamental MPQ compression type; despite being stored as ADPCM, readers see only the uncompressed PCM audio, which the streaming system plays.

ThunderGraft works by simply replacing this system with its own, built on miniaudio. This involves hooking all calls to DDA functions in the game's import table, as well as GetProcAddress for games that look them up at runtime. The sounds are read through Storm, so they can come from MPQs or disc files as before, and are decoded and played on ThunderGraft's own output device. ThunderGraft's audio engine starts when the game initializes the DDA system; if it can't start, ThunderGraft passes everything on to Storm, and the game plays its music as it would without ThunderGraft.

One optional feature of the DDA system is the ability for the caller (the game) to supply its own DirectSound buffer for use in streaming. This not only makes the DDA system use the settings on the buffer, but it also allows the game to modify things such as volume or to get the play state without calling the corresponding DDA function (e.g. IDirectSoundBuffer::SetVolume rather than SFileDdaSetVolume). This is emulated by watching for changes between the IDirectSoundBuffer and playing sound, and updating both as necessary.

For further information, see the source code.

Update History
--------------

Unreleased:
- Replaced FMOD Ex with miniaudio, which is open source and built into the plugin: there is no longer a separate fmodex.dll to ship or find
- Plays music on an output device of its own, at the music's own sampling rate, so the 44 khz workaround is gone
- Supported formats are now WAV, MP3, Ogg Vorbis and FLAC; WMA, AIFF, MP2, VAG, MIDI and tracker formats are no longer supported
- Removed the StarEdit integration
- Builds with GCC (mingw-w64) on Linux or Windows, with automated tests

2011.7.25.1:
- Fixed the problem where dialogue text in Diablo II would not properly appear when dialogue was set to audio and text

2009.9.22.1:
- Fixed a minor but annoying crash that occurs on game exit on some computers

2009.9.15.1:
- Removed the WAV compression box in StarEdit, as it's not used anyway (see Bugs)
- Hopefully does somewhat less raping of other plugins that happen to be running
- Various minor code cleanup for release

2009.9.13.1:
- Several workarounds to work with FireGraft
- Forces sound card audio format to 44 khz or above
- Worked around a bug in FMOD that prevented playing MP3s that have ID3 tags at the beginning of the file
- Numerous small improvements in robustness

2009.9.3.1: 
- Changed the sound engine from the Aegis Sound System to FMOD Ex, which supports additional file formats and features, as well as being more robust and actively supported
- Now plays non-22khz sounds at the correct sampling rate in Diablo II

2003.4.11.1:
- StarEdit may now be used to import MP3s into maps.

Credits
-------

Research, design, and coding: Justin Olbrantz (Quantam)
Sound engine: miniaudio, by David Reid
Ogg Vorbis decoding: stb_vorbis, by Sean Barrett
Logo: Joel Steudler

Versions up to 2011.7.25.1 used FMOD Ex, by Firelight Technologies Pty, Ltd.

License
-------

ThunderGraft is available under the Common Development and Distribution License (CDDL) 1.0. miniaudio is available as public domain or under the MIT No Attribution license, and stb_vorbis as public domain or under the MIT license; see third_party in the source code.
