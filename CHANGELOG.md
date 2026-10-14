# Changelog

## Unreleased

- Playlists: replace the games' music with playlists of your own, shuffled or in order, without gaps or with crossfades between tracks, in sequences, and with music that changes when the game plays a certain sound and goes back afterwards. StarCraft maps can bring playlists of their own. See `docs/playlists.md`
- Reads the `playlist.txt` files of ShadowFlare's Playlist Plugin
- Problems with playlist files are written to `ThunderGraft.log` next to the game
- `tgplaylist`, a tool that checks, converts and simulates playlist files
- Replaced FMOD Ex with miniaudio, which is open source and built into the plugin: there is no longer a separate `fmodex.dll` to ship or find
- Plays music on an output device of its own, at the music's own sampling rate, so the 44 khz workaround is gone
- Supported formats are now WAV, MP3, Ogg Vorbis and FLAC; WMA, AIFF, MP2, VAG, MIDI and tracker formats are no longer supported
- Removed the StarEdit integration
- Builds with GCC (mingw-w64) on Linux or Windows, with automated tests

## 2011.7.25.1

- Fixed the problem where dialogue text in Diablo II would not properly appear when dialogue was set to audio and text

## 2009.9.22.1

- Fixed a minor but annoying crash that occurs on game exit on some computers

## 2009.9.15.1

- Removed the WAV compression box in StarEdit, as it's not used anyway (see Bugs)
- Hopefully behaves nicer with other plugins that happen to be running
- Various minor code cleanup for release

## 2009.9.13.1

- Several workarounds to work with FireGraft
- Forces sound card audio format to 44 khz or above
- Worked around a bug in FMOD that prevented playing MP3s that have ID3 tags at the beginning of the file
- Numerous small improvements in robustness

## 2009.9.3.1

- Changed the sound engine from the Aegis Sound System to FMOD Ex, which supports additional file formats and features, as well as being more robust and actively supported
- Now plays non-22khz sounds at the correct sampling rate in Diablo II

## 2003.4.11.1

- StarEdit may now be used to import MP3s into maps.
