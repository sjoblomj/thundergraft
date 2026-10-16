# ThunderGraft

![ThunderGraft logo](src/TGLogo.bmp)

**Better music for Diablo, Diablo II, StarCraft and Warcraft II: Battle.net Edition.**

ThunderGraft is a plugin for [MPQDraft](https://github.com/sjoblomj/MPQDraft) that lets these classic Blizzard games play music in modern formats, and lets mods give them soundtracks that shuffle, crossfade and react to what happens in the game.

Originally developed by Quantam (Justin Olbrantz) 2003-2011, it was [modernized and improved](CHANGELOG.md) by Ojan (Johan Sjöblom) in 2026.

## What you can do with it

- **Use MP3, Ogg Vorbis and FLAC** in place of the games' WAV files. Your music sounds better and, apart from FLAC, takes far less space than the games' own compressed WAVs.
- **Hear music at its full quality.** The games play their sound at 22 kHz; ThunderGraft plays music on an output of its own, at the music's own sampling rate.
- **Build playlists** with an optional playlist file: play tracks shuffled or in order, without gaps or with crossfades, and switch to other music when something happens in the game, then go back to where you were.
- **Give StarCraft maps their own music.** A map can bring its own playlist file, which plays while the map is loaded.

ThunderGraft works with every version of the supported games, and is a single file with nothing else to install. It changes music and other streamed sounds, such as StarCraft map trigger sounds, but not the games' sound effects.

## Getting started

1. Download `ThunderGraft-<version>.qdp` from the [releases page](https://github.com/sjoblomj/thundergraft/releases). The same page has `tgplaylist-<version>.exe`, if you want to write playlists.
2. Put your music files in an MPQ with an MPQ editor, such as WinMPQ, just as you would WAV files. Don't use WAV compression for anything that isn't a PCM WAV file.
3. Start the game with MPQDraft, with your MPQ and the ThunderGraft plugin.

Music the games look for by a fixed name, such as their opening themes, still has to be named `.wav`, but it can be an MP3, Ogg Vorbis or FLAC file inside: ThunderGraft looks at what a file contains, not at its name. By including a [playlist file](docs/playlists.md), you can make the game play music files under other names and get fine control over playlists, and even switch to other music when something happens in the game.

## Learn more

- [The user readme](docs/README.md): what ThunderGraft does, how to use it, how it works, and its credits
- [Playlists](docs/playlists.md): how to write a playlist file, give a map music of its own, and use `tgplaylist`
- [Changelog](CHANGELOG.md): what has changed in each version
- [Developing ThunderGraft](docs/development.md): building, testing and releasing it, for anyone who wants to work on the code

## License

ThunderGraft is available under the Common Development and Distribution License (CDDL) 1.0. The third-party code in `third_party/` has licenses of its own, listed in [`third_party/README.md`](third_party/README.md).
