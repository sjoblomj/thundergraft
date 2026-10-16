# Playlists

ThunderGraft can replace the games' music with playlists of your own: shuffled or in order, with crossfades, and with music that changes when something happens in the game. A StarCraft map can bring playlists of its own, too.

This page is for modders and map makers. It works the same in Diablo, Diablo II, StarCraft and Warcraft II: Battle.net Edition.

- [A first playlist](#a-first-playlist)
- [Where the playlist file goes](#where-the-playlist-file-goes)
- [When something goes wrong](#when-something-goes-wrong)
- [How it works](#how-it-works)
- [The playlist file](#the-playlist-file)
- [Examples](#examples)
- [Playlists in StarCraft maps](#playlists-in-starcraft-maps)
- [The tgplaylist tool](#the-tgplaylist-tool)
- [Older playlist.txt files](#older-playlisttxt-files)
- [Limitations](#limitations)

## A first playlist

Put this in a file called `playlist.toml`:

```toml
version = 1

[on]
'music\title.wav' = "title"
'music\terran?.wav' = "terran"

[playlist.title]
tracks = ['music\my_title.ogg']

[playlist.terran]
order = "shuffle"
crossfade = 2
tracks = [
    'music\terran_a.ogg',
    'music\terran_b.mp3',
    'music\terran_c.flac',
]
```

Then add it to your mod's MPQ as `music\playlist.toml`, along with the tracks it names, and load the MPQ with MPQDraft and ThunderGraft as usual.

In StarCraft, the title screen now plays `my_title.ogg` instead of `title.wav`, and whenever the game starts one of its Terran tracks (`terran1.wav`, `terran2.wav` and so on), one of your three plays instead, shuffled, so that all three play before any of them repeats, with a two-second crossfade between them.

Tracks can be in any format ThunderGraft plays: WAV, MP3, Ogg Vorbis or FLAC.

## Where the playlist file goes

ThunderGraft looks for the playlist file when the game starts its sound, and uses the first of these it finds:

1. `music\playlist.toml` in the game's archives, including the ones MPQDraft loads
2. `music\playlist.txt` in the archives (see [Older playlist.txt files](#older-playlisttxt-files))
3. `music\playlist.toml` in the game's directory, on disk
4. `music\playlist.txt` in the game's directory

This is the *base file*. A playlist file on disk may name tracks anywhere on the computer, such as `'C:\My Music\theme.ogg'`; one in an archive may only name files relative to the game, such as `'music\theme.ogg'`, which can be in an archive or in the game's directory, as with the game's own files.

StarCraft maps can contain a playlist file of their own; see [Playlists in StarCraft maps](#playlists-in-starcraft-maps).

## When something goes wrong

**A playlist file with errors isn't used at all**, and the game plays its own music. ThunderGraft doesn't try to use the parts that work, as a half-working playlist is harder to make sense of than one that plainly doesn't work.

**Problems are written to `ThunderGraft.log`**, next to the game's program (or in the Windows temporary directory if the game's directory can't be written to). The file is only made when there is something to report, and it starts afresh each time the game starts. Each problem names the file and the line:

```
music\playlist.toml:12: error: playlist "terran" has an unknown key "tracsk"; a playlist may have tracks, order, play, times, crossfade, next and on
```

Errors are typing mistakes, unknown keys, values of the wrong kind, and names of playlists that don't exist. Warnings, such as a track that can't be found when it's time to play it, don't stop the file from being used: the missing track is skipped, and if none of a playlist's tracks can be played, the game's own music plays instead. Each missing track is only reported once.

**The [tgplaylist tool](#the-tgplaylist-tool) finds the same problems without starting the game**, in the same words, and can check that your tracks exist. It can also show what would play when, which is the quickest way to work out why a playlist that loads plays the wrong thing.

**To see every change of music as it happens**, set the environment variable `THUNDERGRAFT_LOG` to `transitions` before starting the game, for example by starting MPQDraft from a command prompt:

```
set THUNDERGRAFT_LOG=transitions
MPQDraft.exe
```

Every time the game starts a sound, `ThunderGraft.log` then says which rule applied and what plays. Leave it off otherwise, as the log keeps growing for as long as the game runs.

With [debug logs](README.md#settings) turned on in ThunderGraft's settings, problems and every change of music are written to the debug log instead, without setting `THUNDERGRAFT_LOG`.

## How it works

The games play music, and some other sounds, by *streaming* them: StarCraft's music and map sounds that triggers play, Diablo II's music and dialogue, and so on. Each time the game starts streaming a file, ThunderGraft looks at the file's name and decides what plays: the game's own file, or one of your playlists in its place. Sound effects such as gunfire aren't streamed, and playlists can't replace them.

Three ideas are enough to understand everything else:

- **Transitions** say "when the game plays this file, play that playlist": `'music\title.wav' = "title"`. They are written in `[on]` tables.
- **The current playlist** is the playlist playing as the game's music. Playlists can have transitions of their own, which only apply while they are the current playlist, so the same file can lead to different music depending on what plays now. That is how you make *modes*, such as darker music after something happens in a mission.
- **A visit** is one go at a playlist. By default, a visit plays one track, and the next visit plays the next track. With `play = "all"`, a visit plays the whole list. When a visit is over, the playlist's `next` says what comes after it; without a `next`, the game is told its music has ended, and does what it would without ThunderGraft, usually starting its next track, which can lead to a transition of its own.

That's all: there are no variables or conditions. To make the music behave differently in some situation, make a playlist for that situation.

### Which files are music

Files under `music\` are music, and in Diablo II, files under `data\global\music\`. Only music changes the current playlist. Replacing a sound that isn't music, such as a line of dialogue, doesn't affect the music. The `music` key can change which files count as music, but you shouldn't need it.

### Which transition applies

When the game starts a file, ThunderGraft looks for a transition in this order, and uses the first it finds:

1. the current playlist's own `on`
2. a StarCraft map's `[on]`, while a map with a playlist file is loaded
3. the base file's `[on]`

Within one table, patterns can match several files with `*` (any characters, including none) and `?` (any one character). If several patterns in a table match, the most specific wins: a pattern without `*` or `?` beats any with them, and otherwise the one with the most characters other than `*` and `?` wins. If two equally specific patterns match, the one that comes first alphabetically wins, and the log says so.

Names are compared the way the games compare them: upper and lower case are the same, and so are `/` and `\`.

## The playlist file

The playlist file is written in [TOML](https://toml.io), a simple format for settings. Lines starting with `#` are comments.

### Writing paths

**Write paths in single quotes**: `'music\terran1.wav'`. In double quotes, TOML treats `\` as the start of a special character: `"music\terran1.wav"` would turn `\t` into a tab. ThunderGraft reports that as an error, but single quotes avoid it. Alternatively, use `/` instead of `\`, which works in either kind of quotes: `"music/terran1.wav"`.

Playlist names are written in double quotes, like other text: `"terran"`.

### The top level

| Key | Value | Meaning |
|---|---|---|
| `version` | `1` | Required, at the top of the file. |
| `[on]` | pattern = target | Transitions that apply whatever is playing. |
| `[playlist.<name>]` | see below | One table for each playlist. |
| `music` | list of patterns | Which files are music. The default is `['music\*', 'data\global\music\*']`. |
| `map_playlists` | `true` or `false` | Base file only. Whether StarCraft maps' playlist files are used; `true` unless set. |
| `inherit` | `true` or `false` | Maps' files only; see [Playlists in StarCraft maps](#playlists-in-starcraft-maps). |

`back` and `stop` have a meaning of their own, so they can't be used as playlist names.

### Playlists

```toml
[playlist.terran]
order = "shuffle"
crossfade = 1.5
tracks = [
    'music\t1.ogg',
    { file = 'music\t2.ogg', times = 2 },
    'music\t3.ogg',
]
next = "zerg"
on.'staredit\wav\darkness.wav' = { music = "dark_terran", crossfade = 3 }
```

| Key | Values | Default | Meaning |
|---|---|---|---|
| `tracks` | list | required | The tracks. Each is a path, or `{ file = <path>, times = <n> }` to play it `n` times in a row. |
| `order` | `"in order"` or `"shuffle"` | `"in order"` | Shuffling works like a shuffled deck of cards: every track plays once before any repeats, and the track that ended one round never starts the next. |
| `play` | `"one"` or `"all"` | `"one"` | How much one visit plays: the next track, or the whole list. |
| `times` | number, or `"forever"` | `1` | With `play = "all"`: how many times one visit plays the whole list. It's the total, not the number of repeats: `times = 2` plays the list twice. Each time is shuffled anew. |
| `crossfade` | seconds | `0` | How long the tracks of this playlist overlap, one fading out as the next fades in. |
| `next` | target | none | What plays when a visit is over, in the same stream. See [Targets](#targets). |
| `on` | pattern = target | none | Transitions that only apply while this is the current playlist. Write each as `on.'<pattern>' = <target>`, as above, or put them in a `[playlist.<name>.on]` table. |

Seconds can have decimals: `1.5`.

**How far a playlist has got is remembered.** With `play = "one"`, each visit plays the track after the one the previous visit played, for as long as the game runs. That matters in StarCraft, which starts new music after every track: your playlist goes through all its tracks rather than starting over each time. With `play = "all"`, each visit starts from the beginning of the list.

**Looping.** When the game asks for a sound to loop, as most games do with their title music, a visit never ends: a `play = "one"` playlist loops its track, and a `play = "all"` playlist repeats its list, reshuffled each time.

### Targets

A target is what a transition plays. It's used in `[on]`, in a playlist's `on`, and in `next`.

| Target | Meaning |
|---|---|
| `"<playlist>"` | Play that playlist in place of the file the game started. If it's the current playlist and still playing, it isn't restarted, but carries on. |
| `"back"` | Go back to the music that was playing before the last interruption, from where it was. See [Back](#back). |
| `"stop"` | Play nothing; the game is told the sound ended. |
| `{ play = <target>, ... }` | The same, with fades. |
| `{ music = <target>, ... }` | Leave the sound the game started alone, and change the music instead. This is how an event changes the music. If no music is playing, nothing happens. |

The fades, all in seconds, for `play` and `music` targets:

- `fade_out`: what played before fades out over this long, and then the new music starts
- `fade_in`: the new music starts silent and fades in over this long
- `crossfade`: both at once, the old fading out while the new fades in

`fade_out` and `fade_in` can be used together; `crossfade` can't be combined with either. Without fades, the change is immediate.

In `next`, `fade_out` fades out the end of the visit's last track, and `crossfade` starts the next playlist that long before the last track ends.

### Back

When a transition changes the music because the game started a sound, it *interrupts* the music that was playing. `"back"` returns to it, from where it was interrupted, which can be the game's own music. That lets a short piece of music, such as a boss theme, play and then hand back to what was playing before:

```toml
[on]
'staredit\wav\boss.wav' = { music = "boss", fade_out = 2 }

[playlist.boss]
play = "all"
tracks = ['music\boss_intro.ogg', 'music\boss_loop.ogg']
next = "boss_end"

[playlist.boss_end]
tracks = ['music\boss_end.ogg']
next = { play = "back", fade_in = 2 }
```

`next` doesn't interrupt anything, so a chain of playlists linked by `next` can go `back` at its end, as `boss_end` does.

Only the last interruption is remembered. Once ThunderGraft has gone back, there's nothing left to go back to until the next interruption. A `"back"` with nothing to go back to does nothing in `on`, and as `next` it's as if there were no `next`. If the game ends the music that was interrupted, for example by starting other music, there's nothing to go back to either.

### What the game sees

The game doesn't know about playlists. As far as it can tell, the sound it started plays until the visit is over and there's no `next`; then the sound ends, and the game carries on as usual. The position and length it sees are those of the track playing.

If the game stops the music while a playlist plays (a victory screen, leaving a game), the playlist stops, and there is no current playlist until the next transition. If the game starts new music while a playlist plays, the transitions for the new music's file decide what happens, as always.

The game's volume settings, and muting when the game is in the background, apply to playlists as they do to the game's own music.

## Examples

### Replace one file with another

```toml
version = 1

[on]
'music\title.wav' = "title"

[playlist.title]
tracks = ['music\title.ogg']
```

### Replace all music with one long list

```toml
version = 1

[on]
'music\*' = "everything"              # Diablo, StarCraft, Warcraft II
'data\global\music\*' = "everything"  # Diablo II

[playlist.everything]
order = "shuffle"
tracks = ['music\a.ogg', 'music\b.ogg', 'music\c.ogg', 'music\d.ogg']
```

### Music that changes when something happens

A StarCraft map can play a sound with a trigger to change the music: the sound can be a short silent file, used only as a signal. Here, once a Terran mission plays `darkness.wav`, the music turns dark and stays dark, because `dark_terran` sends the game's Terran tracks to itself. Going back to the title screen goes to `title`, which ends the dark mode.

```toml
version = 1

[on]
'music\title.wav' = "title"
'music\terran?.wav' = "terran"

[playlist.title]
tracks = ['music\title.ogg']

[playlist.terran]
tracks = ['music\t1.ogg', 'music\t2.ogg']
on.'staredit\wav\darkness.wav' = { music = "dark_terran", crossfade = 3 }

[playlist.dark_terran]
tracks = ['music\dark1.ogg', 'music\dark2.ogg']
on.'music\terran?.wav' = "dark_terran"
```

### Replace a line of dialogue

Any streamed sound can be replaced, not just music, and the replacement can depend on the current playlist. While `dark_terran` from the previous example plays, this plays a different transmission in place of `report.wav`, without touching the music:

```toml
[playlist.dark_terran]
tracks = ['music\dark1.ogg', 'music\dark2.ogg']
on.'music\terran?.wav' = "dark_terran"
on.'staredit\wav\report.wav' = "report_dark"

[playlist.report_dark]
tracks = ['staredit\wav\report_dark.ogg']
```

### Play a list once, then something else, then back to the game

```toml
[playlist.intro]
play = "all"
tracks = ['music\intro1.ogg', 'music\intro2.ogg']
next = "main"

[playlist.main]
play = "all"
times = 3
tracks = ['music\main.ogg']
```

`main` plays three times, and then the game is told its music ended.

## Playlists in StarCraft maps

A StarCraft map is an MPQ archive, so it can contain a `music\playlist.toml` of its own, used while the map is loaded. When the map is unloaded, the base file applies again as if the map's file had never been there.

A map maker can't know which mod the player uses, so a map's file adds to the base file rather than replacing it:

- **Transitions:** the map's `[on]` comes after the current playlist's `on`, and before the base file's `[on]` (see [Which transition applies](#which-transition-applies)).
- **Playlists:** names are looked up in the map's file first, then in the base file's. A map can use the base file's playlists without copying them, and a map playlist with the same name as one in the base file takes its place while the map is loaded, also for the base file's own transitions.
- **Names neither file has** aren't an error in a map's file, as the map may be meant for a mod that has them. They are reported in the log, and transitions to them are ignored.
- **`music`:** the map's, if it has one, otherwise the base file's.
- **`inherit = false`** in the map's file ignores the base file completely while the map is loaded, for maps that want full control.

When the map is unloaded, every track that started while it was loaded stops at once, and the music carries on as if the game had just started it, under the base file. The base file's playlists continue where they were before the map.

Maps come from strangers, so their playlist files have limits: at most 64 KB, 256 playlists, 2048 tracks and 1024 transitions, and tracks can only be files relative to the game, not anywhere on the computer. A player or mod that doesn't want maps to change the music can turn their playlist files off in the base file:

```toml
map_playlists = false
```

While a map is loaded, a file in the map with the same name as one of the mod's tracks is found before the mod's, as with every file the game reads, so give your tracks names a map is unlikely to use.

## The tgplaylist tool

`tgplaylist` checks, converts and tries out playlist files without starting a game. It's released alongside ThunderGraft as `tgplaylist.exe`, and runs from a command prompt.

### Checking a playlist file

```
tgplaylist check playlist.toml
```

reports every problem, with line numbers, exactly as `ThunderGraft.log` would. It also warns about patterns in one table that are equally specific and can match the same file. To also check that the tracks exist, give it the directories or archives to look in:

```
tgplaylist check --dir C:\StarCraft --mpq mymod.mpq playlist.toml
```

By default, it checks the file as one that goes in an archive, where tracks can't have absolute paths; add `--on-disk` for a file that goes in the game's directory.

For a map's file, use `--map`. It then lists the playlists the map expects the base file to provide, or, with `--base`, checks the two together:

```
tgplaylist check --map map_playlist.toml --base playlist.toml
```

### Trying out a playlist file

```
tgplaylist simulate playlist.toml events.txt
```

plays a list of events and prints, after each, which transition applied, what plays, and the current playlist. The events file has one event per line:

| Event | Meaning |
|---|---|
| `start <file> [loop] [as <name>]` | The game starts a sound. Sounds are called `s1`, `s2` and so on, unless named. |
| `end-track [<sound>]` | The track playing plays to its end. |
| `end-visit [<sound>]` | Every track of the playlist's visit plays to its end. |
| `stop [<sound>]` | The game stops the sound. |
| `wait <seconds>` | Time passes. |
| `length <file> <seconds>` | How long a file is; 120 seconds unless given. |
| `missing <file>` | The file can't be found. |
| `load-map <map playlist.toml> [as <name>]` | A map with this playlist file is loaded. |
| `unload-map [<name>]` | The map is unloaded. |

Without a sound, events are about the music. Lines starting with `#` are comments, and paths with spaces go in quotes. For example:

```
# The title screen, then a Terran mission with a boss
start music\title.wav loop
stop
start music\terran1.wav
end-track
stop
start music\terran2.wav
start staredit\wav\boss.wav as cue
end-visit
```

which prints, in part:

```
> start staredit\wav\boss.wav as cue
  start 'staredit\wav\boss.wav' in cue: [on] 'staredit\wav\boss.wav' (playlist.toml:5) -> { music = "boss", fade_out = 2 }
  the music (s3) plays 'music\boss_intro.ogg' of "boss"
= current playlist: "boss"; music: s3, 'music\boss_intro.ogg' of "boss", once the music before it has faded out
```

Remember that the game reacts to sounds ending: when StarCraft's music ends, it stops that sound and starts its next track. The simulator doesn't do that by itself, so write those events out, as above.

`--seed <number>` changes the order shuffled playlists play in; the same seed always gives the same order.

### Converting an older playlist

```
tgplaylist convert playlist.txt playlist.toml
```

See the next section.

## Older playlist.txt files

ThunderGraft also reads the `music\playlist.txt` files of ShadowFlare's Playlist Plugin, so mods made for it work with ThunderGraft as they are, and with ThunderGraft's file formats and fading. To build on such a mod, convert its playlist once with `tgplaylist convert` and continue from the result.

Using ShadowFlare's Playlist Plugin itself is discouraged, and running it together with ThunderGraft is not supported: both would act on the same playlist, and each would get in the other's way.

## Limitations

- Only streamed sounds can be replaced or change the music. Sound effects, unit voices and the like are loaded once and played by the game itself, so ThunderGraft never sees them play.
- There are no variables. Counting ("only the first three times") takes a chain of playlists, and independent modes (light and dark music for each of three races) take a playlist for each combination.
- `back` remembers one interruption, not several.
- Running alongside other plugins works, as long as they pass on the game's calls to open files, as plugins normally do. A plugin that changes file names before ThunderGraft sees them changes what the transitions match.
