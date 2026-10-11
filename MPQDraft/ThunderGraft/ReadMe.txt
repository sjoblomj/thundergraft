THUNDERGRAFT

What It Is
----------

ThunderGraft is an MPQDraft plugin that adds additional music-playing capabilities to Diablo, Diablo II, StarCraft, and Warcraft II: Battle.net Edition. In addition to PCM WAV audio files, which are supported natively by these games (either used uncompressed or compressed via MPQ WAV compression, which is based on lossy ADPCM compression), ThunderGraft adds the ability to play newer, superior audio compression formats, including MPEG Layer-3 Audio (MP3), Ogg Vorbis (Ogg), Windows Media Audio (WMA) without DRM, and Free Lossless Audio Compression (FLAC); all of these give noticeably higher audio quality and (with the exception of FLAC) significantly smaller file sizes than MPQ WAV compression. ThunderGraft uses FMOD Ex for audio decoding, and as such supports all formats FMOD Ex supports out of the box; see File Format Support in http://www.fmod.org/index.php/products/fmodexdetailed for the full list.

One additional feature of ThunderGraft is that it forces the host games to use 44 khz sound. All of the supported games set the sound card to output 22 khz 16-bit stereo audio (although some sound cards have been observed to ignore this and use a higher sampling rate), as that is what the games' sound effects and music all use. However, with superior compression formats available in ThunderGraft it may be desirable to also use higher sampling rate music for higher audio quality. ThunderGraft ensures that 44 khz quality will always be available.

ThunderGraft is game- and version-independent with regard to music file support; however, integration with StarEdit (described below) is version-dependent (although StarEdit has not been modified since version 1.08, many years ago). ThunderGraft is limited, however, to music and other streamed sounds, including StarCraft map trigger sounds; it does not work with sound effects that are not streamed.

How to Use It
-------------

ThunderGraft is easy to use. Simply put the music files in an MPQ with WinMPQ or some other MPQ file editor (as with WAV files prior to ThunderGraft), then load the MPQ with MPQDraft and the ThunderGraft plugin; do NOT use WAV compression when importing non-PCM WAV files into an MPQ. Note that global music files, such as the opening themes of the various games, have hard-coded paths that require the audio files have a WAV extension; ThunderGraft does not rely on extension, and will play them correctly even if they are not actually WAV files.

ThunderGraft also integrates with the StarCraft map editor (StarEdit). When StarEdit is loaded with MPQDraft and ThunderGraft, non-WAV trigger sounds may be imported just like WAV sounds, then subsequently played in StarEdit or StarCraft with MPQDraft and ThunderGraft.

When running MPQDraft, ThunderGraft requires the fmodex.dll file to be in the system search path (e.g. Windows\System32 or the MPQDraft directory) or the same directory as the ThunderGraft plugin itself. This file is included in downloadable versions of ThunderGraft, but is separate to allow updates independently from ThunderGraft when new versions of FMOD Ex are released. This file is automatically incorporated into Self-Executing MPQs (SEMPQs) that use ThunderGraft, but may be overridden by a copy of fmodex.dll in the search path of the computer running the SEMPQ; in this way FMOD Ex may be updated even after the SEMPQ has already been created and downloaded.

Known Bugs/Side Effects
-----------------------

A peculiar bug in FireGraft prevents ThunderGraft from finding the fmodex.dll file in the same directory as the ThunderGraft plugin if this is not also the directory where FireGraft resides. For this reason, when using ThunderGraft with FireGraft this file must be in the FireGraft directory or the system search path. This problem only affects the FireGraft GUI, and does not affect SEMPQs created with FireGraft.

When used with ThunderGraft, StarEdit does not check file format when importing sounds. This means it is possible to import files that are not sounds and cannot be played either by ThunderGraft in StarEdit or StarCraft. A corollary of this is that WAV sounds are not imported with WAV compression.

How It Works (Technical)
------------------------

ThunderGraft targets the Storm DDA system (SFileDda). This system handles streaming sounds from archives and disc files. This system supports only PCM WAV audio files; StarCraft added support for lossy ADPCM compression by adding ADPCM compression as a fundamental MPQ compression type; despite being stored as ADPCM, readers see only the uncompressed PCM audio, which the streaming system plays.

ThunderGraft works by simply replacing this system with its own (FMOD Ex). This involves hooking all calls to DDA functions, as well as a few other things. A DirectSound system is created by the game and then passed to SFileDdaInitialize; as FMOD Ex requires that it create the DirectSound system, DirectSoundCreate must be hooked as well. Finally, ThunderGraft must hook several functions in StarEdit related to sound importing in order to allow the importing of non-WAV files and determination of the length of imported sounds.

One optional feature of the DDA system is the ability for the caller (the game) to supply its own DirectSound buffer for use in streaming. This not only makes the DDA system use the settings on the buffer, but it also allows the game to modify things such as volume or to get the play state without calling the corresponding DDA function (e.g. IDirectSoundBuffer::SetVolume rather than SFileDdaSetVolume). This is emulated by watching for changes between the IDirectSoundBuffer and playing sound, and updating both as necessary.

For further information, see the source code.

Update History
--------------

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
Sound engine: Firelight Technologies Pty, Ltd.
Logo: Joel Steudler