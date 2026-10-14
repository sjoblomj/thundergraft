# Playlists and Audio Triggers: design

Status: implemented, but not yet tried in the games. The user guide is [`playlists.md`](playlists.md). Decisions made while implementing it, where this design left room, are in [Implementation notes](#implementation-notes).

## Goal

Let modders replace the games' music with playlists, as ShadowFlare's Playlist Plugin (SFPls) does, but with more control:

- Turn loading of wav files into other formats
- create playlists with shuffled or ordered playback
- control how many times a track or a whole playlist plays
- crossfades between tracks
- sequences: after playlist A has finished, play playlist B
- Audio Triggers: when the game plays a certain sound file, change the music, optionally fading out the previous music, and go back to it afterwards
- replacing other streamed sounds too, such as turning one dialogue line into another
- StarCraft maps should be able to contain playlists of their own, used while the map is loaded

ThunderGraft's own playlists are written in TOML and is placed in `music\playlist.toml`. SFPls's playlists, in both its formats, are supported through a converter to that format (see below).

## How SFPls works

SFPls reads `music\playlist.txt` and has two formats.

Format 1 is a plain list of files, which replaces all music:

```
music\title.wav
music\terran1.wav
music\zerg1.wav
```

"All music" means every file the game opens under its music directory: `music\` in Diablo, StarCraft and Warcraft II, and `data\global\music\` in Diablo II. Each is replaced by the next file in the list.

Format 2 starts with `<SFPlaylist1>`, maps files the game plays to named playlists, and lists each playlist's files under a `<name>` header:

```
<SFPlaylist1>
music\terran1.wav = terran
music\terran2.wav = terran

<terran>
music\mymusic1.wav
music\mymusic2.wav
music\mymusic3.wav
```

SFPls hooks only `SFileOpenFile` and `SFileOpenFileEx`. It swaps the file name when the game opens a file and then gets out of the way. So it picks a track once per open, and the game decides when the next open happens; its readme notes that a game playing a single looping file never moves on to the next entry.

ThunderGraft works at the other end: it plays every DDA stream itself (`src/DdaPlayer.cpp`), so it controls when tracks change, fades, gaps and loops. What it lacks is file names; it only sees the handles the game passes to `SFileDdaBegin(Ex)`.

## Challenges

### Knowing which file is playing

ThunderGraft has to hook `SFileOpenFile` and `SFileOpenFileEx` as well, and remember which name each handle was opened with, until `SFileCloseFile`. Names need normalizing before they're compared:

- case-insensitive when reading MPQ archives
- `/` and `\` treated the same
- We need to support archive paths (`MUSIC\X.WAV`) as well as paths on the local system (`C:\StarCraft\music\x.wav`).
  * Paths on the local system may only be loaded if the playlist files are loaded from disk rather than the archive (see [Reading the playlist file](#reading-the-playlist-file)).

### Events only see streamed sounds

Music, Diablo II dialogue and StarCraft map trigger sounds go through DDA, so they can cause transitions. Unit sounds and other effects don't: they're loaded once, cached, and played through the game's own DirectSound buffers. Opening a file isn't playing it, so an event on a file open would fire once, or at the wrong time. Catching effects as they play would mean hooking DirectSound's COM interfaces, which is out of scope here.

That still leaves a useful idiom: a StarCraft map can "Play WAV" a short silent cue file just to signal ThunderGraft, which gives map makers scripted music changes with no new trigger types.

### The game still thinks it controls the stream

StarCraft waits for a track to end and then picks the next one. Diablo II polls the buffer status and crossfades between areas itself. So with a playlist playing in place of the game's music, ThunderGraft has to decide what the game sees:

- When is the game told the music ended: after each track, or only after the whole playlist?
- When the game starts different music, or stops it (victory screen, leaving a game) while a playlist is playing, which one wins?
- After a cue switches the music, can the previous music continue from where it was interrupted?
- What do `SFileDdaGetPos` and the game's buffer report while a playlist plays? Position and length must keep making sense to the game, and must not reach the end before the game is meant to see it.

[Behaviour](#behaviour) answers these.

### Which stream is "the music"?

Playlists can play in any stream: in place of the game's music, but also in place of a dialogue line. Only the music has state, though: the current playlist and the `back` point. That state has to outlast individual streams, as StarCraft opens a new stream for every track, and a mode like the dark music in the [example](#example) must survive that. So ThunderGraft has to know which streams are music:

- Replacing music changes the current playlist; replacing a dialogue line must not, or every line of dialogue would knock the music out of its mode.
- A cue that changes the music has to find the music among the streams playing, even when that's the game's own, unreplaced track. Diablo II plays dialogue through DDA while music is playing.
- `back` has to know where to return to.

ThunderGraft can't work this out by itself: to Storm, music and dialogue are both just streamed files, and hints such as the looping flag differ between games. So the music is decided by file name, with the top-level `music` key. The default, `music\*` and `data\global\music\*`, is right for every game, as each only uses its own paths, so most playlist files never set it.

### Audio engine work

`IAudioEngine`/`IAudioSound` (`src/AudioEngine.h`) need:

- fades, combined with the game's volume and the focus muting, rather than replacing them
- two sounds of the same stream playing at once, for crossfades (the engine already plays several sounds at once)
- gapless changes between tracks, by opening the next track before the current one ends, or through miniaudio's data source chaining

Shuffle should work like a shuffled deck: every track plays once before any repeats, and the same track never plays twice in a row across a reshuffle. See [Visits](#visits) for when a playlist reshuffles.

### Other plugins

Other MPQDraft plugins may hook the same Storm functions. ThunderGraft patches import entries by ordinal and calls whatever an entry pointed to before it, which may be another plugin's hook (`CThunderGraftPlugin::PatchImportEntry`), so the hooks chain and every plugin still runs. That is already how its DDA and `SFileCloseFile` hooks work. The new hooks must:

- pass arguments, return values and `GetLastError` through unchanged, and never fail a call themselves
- be returned by `PatchGetProcAddress` too, for games that look Storm functions up at runtime
- lock the handle → name map, as Diablo II opens files from several threads
- be cheap, as the open functions are called for almost every file the game opens

Two consequences:

- The name ThunderGraft sees can depend on load order: a plugin that rewrites names, and hooked after ThunderGraft, runs first.
- A plugin that bypasses the chain, by patching the game's imports to the real Storm function, leaves ThunderGraft without names. A stream with no known name plays as the game asked, with no transitions: playlists stop working, the music doesn't.

The hook tests can cover this with a fake second plugin hooking before and after ThunderGraft.

### SFPls and ThunderGraft together

Running SFPls and ThunderGraft together is not supported, and won't be. ThunderGraft reads SFPls's playlists itself, so there's no need for both, and together they would interfere: both hook `SFileOpenFile(Ex)`, so which name ThunderGraft sees depends on the order the plugins hooked in, and a `music\playlist.txt` would be applied by both. ThunderGraft doesn't detect SFPls or work around it.

The user readme should say that using SFPls is discouraged, and that running it alongside ThunderGraft is not supported.

### Telling users about mistakes

Release builds don't write a log today, so without something more, a typo in a playlist would just mean silence or the game's own music, with no hint why. So:

- **A file with errors isn't used at all.** ThunderGraft doesn't skip the broken parts and run the rest: a half-working setup is harder to make sense of than one that plainly doesn't work. Without a usable base file, the game behaves as if there were no playlist file, and its own music plays. A map file with errors is rejected the same way, and the base file keeps working underneath. Errors include TOML syntax errors, unknown keys, unknown playlist names (except in a map's file, see [How a map's file combines with the base](#how-a-maps-file-combines-with-the-base)), values of the wrong type, paths with control characters, and, for maps, going over the [limits](#maps-are-untrusted). An SFPls file that the converter can't convert cleanly counts as having errors.
- **Problems are written to `ThunderGraft.log`,** next to the game, or in the temporary directory if the game's directory can't be written to. Each entry has the file it's about, a line number where there is one, and the same text that `tgplaylist check` prints. The log is only written when there's something to say, so there is nothing to switch on: a user whose music doesn't change finds the reason in a file next to the game. It's started afresh each time the game starts.
- **Warnings don't reject the file.** Two equally specific patterns, and tracks that turn out to be missing or undecodable when they're played, go in the log, and playback carries on as described in [Behaviour](#behaviour).
- **Logging every transition is opt-in.** That's for working out why a setup that loads plays the wrong thing, and would grow without end in a long session.
- **No message boxes.** StarCraft and Diablo run fullscreen through DirectDraw, and a message box over them tends to minimise the game or leave it stuck behind the box.
- **The `tgplaylist` tool** (see [Tool](#tool)) finds the same errors and warnings before the game is even started, and simulates what plays for a list of events.

Missing files fall back to the game's own file, never to silence.

## The model

A playlist file describes a small state machine in which **the states are the playlists**. At any moment there is at most one *current playlist*, which plays in the music stream. Transitions say "on this event, play that playlist".

Playlists can also play in place of other streams, such as dialogue lines. Those don't change the current playlist: the state is the music's alone (see [Which stream is "the music"?](#which-stream-is-the-music)).

There are two kinds of event:

- **a stream starts**, matched by file name: the game starting music, a dialogue line, or a map's cue sound
- **a playlist finishes** (a [visit](#visits) ends)

and two places to write transitions:

- **inside a playlist**: they apply only while it's the current playlist
- **at the top level**: they apply whatever is current

A playlist's own transitions beat the top-level ones. (A StarCraft map can bring a playlist file of its own, which adds a level in between; see [Map playlists](#map-playlists).) That is the whole language: no variables, no conditions, no rule order. "Which playlist is current" is the only state, and the only condition. To make something behave differently in some situation, make a playlist for that situation.

The design deliberately has no variables or scripting. ThunderGraft can only observe streams starting and ending, so a programming language would compute from the same few inputs, at the cost of a runtime, infinite loops inside the game, and errors that only show at runtime. See [Limitations](#limitations) for what this gives up.

## File format

### Why TOML

The format needs maps (file → playlist), lists of tracks with optional settings, and per-playlist settings. A format of our own would need its own rules for all of this, and its own documentation. TOML and YAML both already have them, and both are familiar from other tools.

TOML is chosen over YAML because:

- Indentation doesn't matter. In YAML, a misplaced space silently moves a setting into a different section.
- No guessing at types. YAML reads some unquoted words as other types: `no` as false, `1.10` as the number 1.1.
- Duplicate keys are an error. That guarantees an event has at most one target in each place transitions are written.
- The parser is small. [toml++](https://github.com/marzer/tomlplusplus) is MIT-licensed, header-only C++17, and builds with exceptions disabled. The YAML parsers (yaml-cpp, rapidyaml) are much larger.

YAML's advantage is that paths can be written unquoted. In TOML they have to be quoted, and the kind of quotes matters; see [Paths](#paths).

### Example

```toml
version = 1

# Transitions that apply whatever is playing
[on]
'music\title.wav'    = "titlelist"
'music\terran?.wav'  = "terranlist"
# A cue: boss.wav itself still plays, as the map intended. The music fades out
# over 3 seconds, then "boss" plays in the music stream. This interrupts the
# music, so "back" can return to it later.
'staredit\wav\boss.wav' = { music = "boss", fade_out = 3 }

[playlist.titlelist]
tracks = ['music\title.ogg']  # Swap the games wav file for our own ogg file

[playlist.terranlist]
order = "shuffle"
crossfade = 1.5
tracks = [
    'music\t1.ogg',
    { file = 'music\t2.ogg', times = 2 },
    'music\t3.ogg',
]
# Only while terranlist is the current playlist: darkness.wav plays as normal,
# and the music crossfades into "dark_terran" over 3 seconds. dark_terran
# becomes the current playlist, so from now on its transitions apply, not
# terranlist's.
on.'staredit\wav\darkness.wav' = { music = "dark_terran", crossfade = 3 }

[playlist.dark_terran]
tracks = ['music\dark_t1.ogg', 'music\dark_t2.ogg']
on.'music\terran?.wav' = "dark_terran"    # stay dark when the game changes track
on.'staredit\wav\report.wav' = "report_dark"    # and swap a transmission for a darker one

[playlist.report_dark]
tracks = ['staredit\wav\report_dark.ogg']

[playlist.boss]
play = "all"
tracks = ['music\boss_intro.ogg', 'music\boss.ogg']
next = "boss_end"

[playlist.boss_end]
tracks = ['music\boss_end.ogg']
next = { play = "back", fade_in = 2 }
```

What this does, in StarCraft:

- The title screen plays `titlelist`. Terran missions play `terranlist` one track at a time, as SFPls would, but shuffled and with crossfades.
- When a Terran mission plays the cue `darkness.wav` (a map's trigger sound), the music crossfades to `dark_terran` and stays dark for the rest of the mission, because `dark_terran` sends the game's Terran tracks back to itself. While it's dark, the map's `report.wav` transmission plays as `report_dark.ogg` instead; the music isn't affected. Returning to the title screen goes to `titlelist`, which ends the dark mode with no reset needed.
- Whenever a map plays `boss.wav`, the current music fades out and `boss` plays its intro and main track, then `boss_end`, and then the music that was interrupted continues from where it was interrupted, fading in.

### Top level

| Key | Value | Meaning |
|---|---|---|
| `version` | `1` | Required. Lets later versions change the format. |
| `music` | list of file patterns | Which streams are music, and so carry the current playlist. Default `['music\*', 'data\global\music\*']`. See [Which stream is "the music"?](#which-stream-is-the-music). |
| `[on]` | file pattern = target | Transitions that apply whatever is current. |
| `[playlist.<name>]` | see [Playlists](#playlists) | One table per playlist. |
| `map_playlists` | `true`, `false` | Base file only; default `true`. Whether maps' playlist files are used. |
| `inherit` | `true`, `false` | Map files only; default `true`. With `false`, the base file is ignored while the map is loaded. |

Unknown keys, unknown playlist names and values of the wrong type are errors, not ignored (a map's file may name playlists the base provides; see [Map playlists](#how-a-maps-file-combines-with-the-base)): a file with errors isn't used, and the errors are written to the log (see [Telling users about mistakes](#telling-users-about-mistakes)). `back` and `stop` can't be used as playlist names.

### Playlists

| Key | Values | Default | Meaning |
|---|---|---|---|
| `tracks` | list | required | Each entry is a path, or `{ file = <path>, times = <n> }` to play it `n` times in a row. |
| `order` | `"in order"`, `"shuffle"` | `"in order"` | |
| `play` | `"one"`, `"all"` | `"one"` | How much one visit plays; see below. |
| `times` | number, `"forever"` | `1` | With `play = "all"`: total plays of the whole list per visit. |
| `crossfade` | seconds | `0` | Between tracks of this playlist. |
| `next` | target | none | What to play when a visit ends, in the same stream. |
| `on` | file pattern = target | none | Transitions that apply while this is the current playlist. |

`times` is always the total number of plays, never extra repeats, as "loop 2" is ambiguous. Durations are numbers of seconds, e.g. `1.5`.

`on` entries are written as TOML dotted keys, one per line (`on.'music\terran?.wav' = "dark_terran"`), or as a `[playlist.<name>.on]` table.

### Visits

Playing a playlist in a stream starts a *visit*. A visit plays:

- with `play = "one"`: the playlist's next track
- with `play = "all"`: the whole list, `times` times

When a visit ends, the stream goes on to `next` if the playlist has one. Otherwise the stream ends, the game is told it ended, and for music the game picks its next file as it would without ThunderGraft; that file may cause a transition in turn. So `next` decides who chooses what comes after: ThunderGraft, or the game. The same holds for other streams: a dialogue line replaced by a chain of playlists only ends, for the game, when the chain does, which matters for Diablo II's "audio and text" setting.

Each visit starts a *pass* through the list, or continues one:

- **`play = "one"`:** a visit continues the current pass, with the track after the one the previous visit played, and a new pass starts when the list runs out. The playlist remembers how far it got for the rest of the session, as SFPls's do. StarCraft asks for new music after every track, so this is what makes every track play before any of them repeats.
- **`play = "all"`:** every visit starts a new pass, from the start of the list, and each of its `times` repetitions is a new pass too. A visit cut short by a transition doesn't carry over to the next visit. (`back` is different: it continues from where the music was interrupted; see [Back](#back).)

With `order = "shuffle"`, every pass is shuffled anew, like a reshuffled deck: within a pass every track plays once, and the last track of one pass is never the first of the next. So a shuffled `play = "all"` playlist plays in a new order on every visit, and a shuffled `play = "one"` playlist reshuffles each time all its tracks have played.

If the game asked for a stream to loop (title screen music, for example), a visit never ends: a `play = "one"` playlist loops its track, and a `play = "all"` playlist repeats its list, with each repetition a new pass.

### Targets

A target is what a transition plays. It acts on the stream the event is about: the stream that just started, or, for `next`, the stream the visit was playing in.

| Target | Meaning |
|---|---|
| `"<playlist>"` | Play that playlist in the stream, in place of the game's file. Going to the current playlist doesn't restart it: if it's still playing, it continues; otherwise a new visit starts. |
| `"back"` | Music only: go back to what was interrupted, from the position where it was interrupted. This may be the game's own music. See [Back](#back). |
| `"stop"` | Silence the stream; the game is told it ended. For music, until the game next starts music. |
| `{ play = <target>, ... }` | The same, with fades: `fade_out` (what played before), `fade_in` (the new playlist) or `crossfade` (both at once), in seconds. |
| `{ music = <target>, ... }` | Leave the stream alone, and play the target in the music stream instead, with the same optional fades. This is how a cue changes the music. In a music stream it's the same as `play`. |

Without fades, the change is immediate. `{ music = ... }` also works in `next`: a dialogue playlist can change the music when it has finished.

### Back

A transition that changes the music because a stream started (a music stream, or a `{ music = ... }` cue) *interrupts* what was playing, and that is what `back` returns to. Replacing another stream, such as a dialogue line, doesn't interrupt anything. A `next` doesn't interrupt anything: it continues the interruption. So a cue can start a chain of playlists, linked by `next`, and the end of the chain can go `back` to the music from before the cue, as `boss_end` does in the [example](#example).

Only one interruption is remembered. If a second cue interrupts the boss music in the example, `back` returns to the boss music, and the music from before the first cue is lost.

Nothing is paused while the interruption plays. ThunderGraft remembers what was playing (a playlist's track, or the game's own file) and the position it had reached, and lets the sound be stopped as any other. `back` opens that track again, seeks to the remembered position, and fades in if asked to. The engine can already seek, so this needs no engine work. The game's own music is read through the game's own file handle. After a cue, the game still has that file open, since as far as it knows its music never stopped. If the game closes the file before the `back`, for example by starting other music, there is nothing left to go back to.

After a `back`, there is nothing left to go back to until the next interruption. A `back` with nothing to go back to, or outside the music, acts as if there were no target: as `next`, the visit ends and the game is told the stream ended; in an `on` transition, nothing happens.

### Which transition applies

When a stream starts:

1. If the current playlist's `on` has a matching pattern, that transition applies.
2. Otherwise, if a [map](#map-playlists) is loaded and its top-level `[on]` has one, that applies.
3. Otherwise, if the base file's top-level `[on]` has one, that applies.
4. Otherwise there is no transition.

If several patterns in one table match the file, the one with the most characters other than `*` and `?` wins; a pattern without wildcards always wins over one with them. Two equally specific matches are a warning (from the tool, and in the log), and the alphabetically first wins.

What happens then:

- **A plain target (or `play`):** it plays in the stream that started, in place of the game's file. If the stream is music, it becomes the music stream, and the target the current playlist. Otherwise the music isn't affected.
- **A `music` target:** the stream that started plays as normal, and the target replaces whatever plays in the music stream. If no music is playing, the transition is ignored.
- **No transition:** the game's file plays. If the stream is music, it becomes the music stream, with no current playlist until the next transition. Otherwise nothing changes.

The music stream is always the most recently started music stream. Diablo II crossfades between areas by starting the new area's music while the old one fades out; the old stream keeps playing whatever it was playing until the game ends it, while the state moves on to the new one.

Transitions only happen on these events, never as a side effect of another transition, so they can't loop.

### Paths

TOML treats `\` in double-quoted strings as the start of an escape. `"music\zerg1.wav"` is an error, but `"music\terran1.wav"` silently becomes `music`, a tab, and `erran1.wav`. So:

- paths are written in single quotes, which TOML takes literally: `'music\terran1.wav'`
- `/` is accepted in place of `\` everywhere: `"music/terran1.wav"` works in either kind of quotes
- a path containing a tab, newline or other control character is reported as an error, suggesting single quotes

The documentation and the converter's output always use single quotes. File patterns may use `*` and `?`.

## Behaviour

Answers to [The game still thinks it controls the stream](#the-game-still-thinks-it-controls-the-stream):

- **Game-visible end:** when a visit ends with no `next` (see [Visits](#visits)), in any stream.
- **Position and length:** the current track's.
- **The game wins:** when the game starts new music, the transitions for that file decide what plays, as for any event. When it ends the music stream, the current playlist stops; there is no current playlist until the next transition, but every playlist keeps its position.
- **Several music streams:** the most recently started one is the music; older ones play out until the game ends them (see [Which transition applies](#which-transition-applies)).
- **Back:** reopens what the last interruption replaced and seeks to where it was; see [Back](#back).
- **Volume:** fades multiply with the game's volume and with focus muting; the game's volume changes keep applying to playlists.
- **Missing or undecodable tracks:** skipped; if a whole playlist is unplayable, the game's own file plays.

## Limitations

Without variables, two things take more writing:

- **Counting**, e.g. "only the first three times the cue plays": a chain of playlists, `C1` → `C2` → `C3`.
- **Independent modes**, e.g. dark/light music for each of three races: one playlist per combination, six instead of three, each with its own transitions.

Anything with a fixed number of situations can still be expressed, so nothing becomes impossible. If the duplicated transitions turn out to be a real problem, the thing to add is named sets of transitions that several playlists share, not variables.

Only the music has state. A replaced dialogue line can depend on the music's state (through the current playlist's `on`), but there is no state of its own, such as "the second time this line plays".

`back` remembers one interruption, not a stack of them. A stack can be added if interruptions of interruptions turn out to matter.

Events are only stream starts and visit ends. Stream ends and timers ("after five minutes of the same music") could be added as further kinds of key later, without changing the model.

## SFPls playlists

### Converting format 2

The converter turns an SFPls format 2 playlist into the TOML format:

| SFPls | TOML |
|---|---|
| `music\terran1.wav = terran` | `'music\terran1.wav' = "terran"` under `[on]` |
| `<terran>` and the files after it | `[playlist.terran]` with those files as `tracks`, in the same order |

SFPls can replace sounds as well as music. Those mappings convert the same way: a plain target replaces the stream it's about, whether it's music or not, so they behave as in SFPls.

Every converted playlist gets the defaults, `order = "in order"` and `play = "one"`, with no `next`, which is how SFPls behaves. The converter writes them out explicitly, so a modder editing the result sees what there is to change.

A mapping to a playlist that doesn't exist, a playlist with no files, and lines that are neither mappings nor files are reported with their line numbers. SFPls section names may contain characters TOML doesn't allow in bare keys; those are quoted (`[playlist."my list"]`). A section named `back` or `stop` is renamed, and the converter says so.

### Converting format 1

Format 1 (a plain list) converts to a single playlist that every file in the game's music directory goes to (see [How SFPls works](#how-sfpls-works)):

```toml
version = 1

[on]
'music\*'             = "music"    # Diablo, StarCraft, Warcraft II
'data\global\music\*' = "music"    # Diablo II

[playlist.music]
order = "in order"
play = "one"
tracks = [
    'music\title.wav',
    'music\terran1.wav',
    'music\zerg1.wav',
]
```

The converter doesn't know which game the playlist is for, so it writes both patterns. That's harmless: each game only uses its own paths. The tracks themselves may be under the music directory too, but they never match: ThunderGraft opens a playlist's tracks itself, and only files the game opens cause events.

### Where the converter runs

The conversion is one piece of platform-independent code, used in two places:

- **In the plugin**, so existing SFPls mods work with ThunderGraft unchanged: when there's no ThunderGraft playlist, `music\playlist.txt` is converted in memory and used as if it were one.
- **In the playlist tool**, so modders can convert once and then extend the result.

### Tool

A small command-line program, built for Windows and natively:

- `tgplaylist convert playlist.txt playlist.toml`: converts an SFPls playlist.
- `tgplaylist check playlist.toml`: reports every problem in a playlist, with line numbers. It can also check that the files the playlist names exist, given a directory or the MPQs to look in. `tgplaylist check --map map.toml` checks a map's file and lists the playlist names it expects the base to provide; with `--base playlist.toml`, it checks the two together, as ThunderGraft will combine them.
- `tgplaylist simulate playlist.toml events.txt`: reads a list of events, such as "start `music\terran1.wav`" or "end of visit", and prints the current playlist, the track and the transition taken after each. This is also how the state machine is tested natively.

## Reading the playlist file

1. If there is a `music\playlist.toml` present in an archive, use that.
2. If not, check for `music\playlist.txt` in an archive and convert it to TOML.
3. If neither is present, check if there is a `music\playlist.toml` present on disk, in the game directory, and use that.
4. If not, check if there is a `music\playlist.txt` present on disk, in the game directory, and convert it.

A `music\playlist.txt` whose first line (ignoring blank lines) is `<SFPlaylist1>` is format 2; any other is format 1.

This is the *base file*. It's read once, the first time ThunderGraft starts its audio engine. Archives open by then, including the ones MPQDraft loads, are part of the base; any archive opened later is treated as a map (see [Map playlists](#map-playlists)).

## Map playlists

A StarCraft map is an MPQ, so it can contain a `music\playlist.toml` of its own, for music while that map is loaded. When the map is done, the base file applies again, as if the map's file had never been there.

### Layers

Playlist files are layers. The base file is the bottom layer. A map's file is laid over it while the map's archive is open, and removed when the archive closes.

ThunderGraft hooks `SFileOpenArchive` and `SFileCloseArchive` as well, with the same chaining as its other hooks:

- **On open:** look for `music\playlist.toml` in that archive only. If it's there, push it as a layer, tied to that archive handle.
- **On close:** remove that archive's layer, then let Storm close the archive.

Layers are kept as a stack, and the most recently opened map is the one in use. That copes with several archives being open at once, and closing removes the right layer wherever it is in the stack.

Maps only use `music\playlist.toml`; SFPls never read playlists from maps, so there are none to convert.

### How a map's file combines with the base

A map maker can't know which mod the player is running, so by default a map adds to the base file rather than replacing it:

- **Transitions:** the map's top-level `[on]` is its own level in [Which transition applies](#which-transition-applies), between the current playlist's `on` and the base file's `[on]`. It isn't merged into the base's `[on]`, as a base pattern without wildcards would then beat a map's wildcard pattern by specificity, which would surprise the map maker.
- **Playlists:** a playlist's name is the part after `playlist.` in its table header, and targets (in `on`, `[on]`, `next`, `play` and `music`) refer to playlists by name. While a map is loaded, every name is looked up in the map first, then in the base, whichever file the reference is in:
  - A map can use the base's playlists without copying them: a map cue `{ music = "boss" }` plays the base's `boss` if the map doesn't define one.
  - A map playlist with the same name as a base playlist hides the base one, for the base's references too. So a map can define its own `terranlist`, and the base's `[on]` sends the game's Terran tracks to the map's music. When the map is unloaded, the base's playlist is back.
  - A map maker can't know which mod the player runs, so a map that names a playlist neither file defines isn't rejected for it, as that's a problem with the combination, not necessarily with the map's file. It's a warning in the log, the transitions that use the name are ignored (a `next` to it acts as if there were no `next`), and the rest of the map's file works. Inside the base file, an unknown name is still an error.
- **`music`:** the map's, if it has one, otherwise the base's.
- **`inherit = false`** in the map's file ignores the base file completely while the map is loaded, for maps that want full control.

### When a map is unloaded

1. **Stop every track ThunderGraft opened while the map was loaded**, before Storm closes the archive. That includes the map's own playlists, but also base playlists' tracks: while the map is loaded, Storm may have found a file of the same name in the map (see below). Storm handles into a closed archive can't be trusted. There's no time for a fade, as the game is waiting for the close to return. The game's own streams are the game's business, as without ThunderGraft.
2. **Work out again what should play.** A map playlist that's current goes away, and so does a `back` point that refers to the map. ThunderGraft knows which file the game's music stream is playing, so it handles it as if that stream had just started under the remaining layers: a base playlist, or the game's own file if no transition matches.
3. **The base file's state is untouched.** Its playlists' passes were frozen while the map was loaded, so the mod's music continues where it left off.

In practice the game usually starts new music at that moment anyway (the score screen, the menus), and step 2 rarely matters.

### Maps are untrusted

Maps are downloaded from strangers, so a map's file is hostile input:

- The reader must cope with anything, with limits on file size and on the number of playlists, tracks and transitions.
- A playlist in an archive may only refer to files in archives: no absolute paths and no `..`, as Storm falls back to opening files on disk.
- The base file can turn map playlists off with `map_playlists = false`, for players and mods that don't want maps changing their music.

While a map is loaded, Storm's search may find a file in the map with the same name as a base track before the base one. That's how Storm works for every file, and ThunderGraft doesn't change it, but modders should know.

### Alternative not taken

Without the archive hooks, ThunderGraft could read `music\playlist.toml` through Storm's normal search every time music starts: Storm prefers the map's archive while it's open, so that finds the map's file, and the base file afterwards. That's less code, but it can't layer (Storm returns one file or the other), only notices an unloaded map at the next music start, and can't stop a map's tracks before their archive closes.

## Implementation order

1. Hook `SFileOpenFile(Ex)` and map handles to names, with tests on the fake `Storm.dll`.
2. A platform-independent playlist reader on toml++, and the SFPls converter for both formats, with native tests and line-numbered errors.
3. The state machine, platform-independent, and the `tgplaylist` tool with `convert`, `check` and `simulate`.
4. Music transitions with `play = "one"` and `order = "in order"`, matching SFPls; compare against SFPls in the games.
5. Engine support for fades and gapless changes.
6. `play = "all"`, shuffle, `times`, crossfades and `next`.
7. Transitions on other streams: replacing them, cues (`music` targets), and `back`.
8. Map playlists: hook `SFileOpenArchive` and `SFileCloseArchive`, layers, and unloading; limits for untrusted files, tested with malformed and oversized files.
9. The user readme: playlists, the `tgplaylist` tool, and that using SFPls is discouraged and running it alongside ThunderGraft is not supported.

## Implementation notes

All nine steps of the [implementation order](#implementation-order) are done, except comparing with SFPls in the games, which needs the games.

### Where it is

| File | What |
|---|---|
| `src/PlaylistPath.*` | Name normalization, pattern matching, specificity, and whether two patterns can match the same file |
| `src/PlaylistFile.*` | The model, and the reader on toml++, with the limits for maps |
| `src/SfplsConverter.*` | The SFPls converter, both formats |
| `src/PlaylistLoader.*` | Finding the base file, in the order of [Reading the playlist file](#reading-the-playlist-file) |
| `src/PlaylistVisit.*` | Passes, shuffling and visits |
| `src/PlaylistLayers.*` | The base file and maps' layers, name lookup, which transition applies, and each layer's passes |
| `src/PlaylistPlayer.*` | The state machine and what it plays: streams, the current playlist, `back`, fades, gapless changes, maps |
| `src/PlaylistSimulator.*` | `tgplaylist simulate`: the real player with a simulated engine |
| `src/StormFileNames.*`, `src/PlaylistLog.*` | The handle → name map, and `ThunderGraft.log` |
| `src/DdaPlayer.*`, `src/ThunderGraft.*` | The DDA emulation now plays every stream through the player; the new hooks |
| `tools/tgplaylist.cpp` | The tool |

Everything but the last three rows is platform-independent and tested natively.

### Behaviour

- **The current playlist when the game ends the music stream.** [Behaviour](#behaviour) says the current playlist stops when the game ends the music stream. Taken literally, that would undo modes such as `dark_terran` in the [example](#example): StarCraft ends each track's stream before starting the next. So the current playlist is only cleared if the game ends the stream while the playlist's visit is still playing (a victory screen cutting the music off). If the visit had ended, and the game was told so, the game is only moving on, and the playlist stays current for the transitions of the game's next music.
- **An interruption of nothing.** A transition on a music stream only replaces the `back` point if something was playing. If the game's music had ended, the `back` point stays as it was.
- **Going to the current playlist from a new music stream.** If it's still playing, the playing visit moves to the new stream, sounds and all, and the old stream ends as far as the game can tell. That makes Diablo II's area changes seamless when both areas lead to the same playlist.
- **Fades on a new music stream.** `fade_out` and `crossfade` in a transition on a new music stream fade out the old music stream, which then ends; with `fade_out`, the new music waits for it. Without fades, the old music stream plays on until the game ends it, as before.
- **`back` to the game's own file** is only possible in the stream it came from, as the game's file handle is only safe to read while the game's stream is alive. Interrupting the game's music with a cue keeps that stream alive, so a cue and `back` work.
- **`stop` with `fade_out`** tells the game the stream ended once the fade is over, so that the game doesn't cut the fade short by ending the stream.
- **Fades in `next`.** `fade_out` fades out the end of the visit's last track; `crossfade` starts the next playlist's first track that long before the last track ends; `fade_in` fades it in. What comes after a track is worked out about two and a half seconds before it ends (longer for long crossfades), and the next track is opened then.
- **Gapless changes** use miniaudio's start times: `IAudioSound::StartAfter` schedules a sound to start in engine frames at the moment the other one ends, accurate to a mixing period. `IAudioSound::Fade` uses miniaudio's fader, which multiplies with the volume and the focus muting. The tests measure both in rendered output.
- **An entry with `times`** plays all its times in one visit, also with `play = "one"`: "play it n times in a row".
- **Stricter than the tables say:** `times` on a `play = "one"` playlist is an error, as it would do nothing; `crossfade` can't be combined with `fade_out` or `fade_in` in one target; patterns that are the same once normalized are duplicates; names are case-sensitive, as TOML keys are.
- **Maps' passes.** Each map layer has passes of its own, copied from the layer below the first time a playlist plays while the map is loaded, so the base file's playlists continue where they were once the map is unloaded.
- **A missing or undecodable track** is warned about once per session. A visit tries each entry at most once before giving up, so a `times = "forever"` playlist with nothing playable can't spin.
- **Equally specific patterns** are found statically by `tgplaylist check`, but at run time only warned about when they actually tie, as comparing every pair of a map's patterns could be slow.

### Logging

Transitions are logged with `THUNDERGRAFT_LOG=transitions` in the environment, like `THUNDERGRAFT_AUDIO`, rather than a key in the playlist file, which a mod could ship with by mistake.

### Map limits

64 KB, 256 playlists, 2048 tracks, 1024 transitions (`on` entries and `next`s) and 64 music patterns. Every file's paths are limited to 260 characters, `times` to a million and durations to an hour.

### Answers to open questions

- **Does `SFileOpenFileEx` with an archive handle search only that archive?** Not under MPQDraft: its extended redirection, on by default, hooks `SFileOpenFileEx` to always search every archive, and its archives have the highest priority, so a mod's `music\playlist.toml` would be found instead of the map's. ThunderGraft reads maps' files with Storm's own `SFileOpenFileEx`, found in Storm's export table rather than through `GetProcAddress` (which MPQDraft hooks too), and checks with `SFileGetFileArchive` that the file is the map's.
- **Are all the archives MPQDraft loads open before the game first calls `SFileDdaInitialize`?** MPQDraft opens its archives when the game opens its first, through its own pointer to Storm, so ThunderGraft's `SFileOpenArchive` hook never sees them and can't take them for maps. In case a game initializes DDA before opening any archive, ThunderGraft looks for the base file once more when the game first starts a stream, if it found none before.

### toml++

Two things about toml++ 3.4.0 needed working around, without changing it:

- It asserts some things about its input before checking them properly. In debug builds that aborts on hostile input, such as a map's file containing `[[[`, and with `NDEBUG` the assertion becomes a hint to the compiler, which makes it undefined behaviour. `PlaylistFile.cpp` includes it without `NDEBUG` and with assertions that do nothing; the checks after them report the errors. The reader's tests include hostile and randomly damaged files, and pass under AddressSanitizer and UndefinedBehaviorSanitizer.
- It parses numbers with string streams on GCC, which bring in locales, whose thread-safe initialization needs functions Windows XP lacks; `plugin_dll_check` caught that. GCC has been able to parse them with `std::from_chars` since GCC 11, which `TOML_FLOAT_CHARCONV` turns on.

## Open questions

- Which StarCraft trigger sounds go through DDA? The readme says map trigger sounds do; check which, in the game.
- A cue while no music plays is ignored. Should it instead start the target, even though the player may have turned the music off?
- Should events ever be allowed on files that are opened but not streamed, documented as unreliable?
- When does StarCraft open and close map archives? To check in the game before building map playlists on it:
  - the map list, which probably opens every map briefly for its description and preview (harmless if the layer comes and goes before any music starts)
  - the lobby, which probably keeps the selected map open; that would let a map set the ready-room music
  - the game itself, and the score screen afterwards: is the archive closed before or after the score screen's music starts?
  - campaign missions, maps downloaded from Battle.net into a temporary file, and replays
- ~~Does `SFileOpenFileEx` with an archive handle search only that archive?~~ See [Answers to open questions](#answers-to-open-questions).
- ~~Are all the archives MPQDraft loads open before the game first calls `SFileDdaInitialize`?~~ See [Answers to open questions](#answers-to-open-questions).
- Does StarCraft open maps with `SFileOpenArchive`, or with `SFileOpenFileAsArchive`, which ThunderGraft doesn't hook? Map playlists depend on the former.
