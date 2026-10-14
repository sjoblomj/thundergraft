/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	A playlist file (music\playlist.toml), and the reader that turns its TOML into one. See docs/playlists.md for the format and docs/playlists-design.md for the reasoning behind it.

	A file with errors isn't used at all: the reader returns nothing, and says why in the diagnostics. A map's file is read with limits on its size and on how much it may contain, as maps come from strangers.

	Platform-independent, so it can be tested anywhere.
*/

#ifndef PLAYLISTFILE_H
#define PLAYLISTFILE_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "PlaylistDiagnostics.h"

enum PlaylistTargetKind
{
	// Play a playlist
	PLAYLIST_TARGET_PLAYLIST,
	// Go back to what the last interruption of the music replaced
	PLAYLIST_TARGET_BACK,
	// Silence the stream
	PLAYLIST_TARGET_STOP
};

// What a transition plays: "name", "back", "stop", { play = ..., fades } or { music = ..., fades }
struct PlaylistTarget
{
	PlaylistTargetKind kind = PLAYLIST_TARGET_PLAYLIST;
	// For PLAYLIST_TARGET_PLAYLIST
	std::string playlist;
	// { music = ... }: play the target in the music stream, rather than in the stream the event is about
	bool bMusic = false;
	// In seconds; 0 for none
	double nFadeOut = 0;
	double nFadeIn = 0;
	double nCrossfade = 0;
	unsigned nLine = 0;

	// How the target is written, for messages: "terranlist", "back", { music = "boss", fade_out = 3 }
	std::string Describe() const;
};

// One entry of an on table: a file pattern and its target
struct PlaylistTransition
{
	// Normalized; see PlaylistPath.h
	std::string pattern;
	// As written in the file, for messages
	std::string written;
	PlaylistTarget target;
	unsigned nLine = 0;
};

struct PlaylistTrack
{
	// As written, with / turned into \ (see PlaylistPathForStorm)
	std::string file;
	// How many times in a row it plays
	unsigned nTimes = 1;
	unsigned nLine = 0;
};

struct Playlist
{
	std::string name;
	std::vector<PlaylistTrack> tracks;
	bool bShuffle = false;
	// play = "all" rather than "one"
	bool bPlayAll = false;
	// With play = "all": how many times one visit plays the whole list
	unsigned nTimes = 1;
	bool bForever = false;
	// Seconds, between tracks of this playlist
	double nCrossfade = 0;
	bool bHasNext = false;
	PlaylistTarget next;
	// Transitions that apply while this is the current playlist
	std::vector<PlaylistTransition> on;
	unsigned nLine = 0;
};

struct PlaylistFile
{
	// What the file is called in messages
	std::string label;
	bool bMap = false;
	// Read from a file on disk rather than from an archive, so its tracks may be anywhere on disk
	bool bFromDisk = false;

	// Patterns of the files that are music. Without a music key, the default applies (PlaylistDefaultMusic).
	bool bHasMusic = false;
	std::vector<std::string> music;

	// The top-level transitions
	std::vector<PlaylistTransition> on;

	std::map<std::string, Playlist> playlists;

	// Base file only: whether maps' playlist files are used
	bool bMapPlaylists = true;
	// Map files only: false to ignore the base file while the map is loaded
	bool bInherit = true;

	// Names a map's file refers to without defining them, which it expects the base file to provide, with the first line each is used on. Always empty for a base file, where such names are errors.
	std::vector<std::pair<std::string, unsigned>> external;

	const Playlist *Find(const std::string &name) const;
};

// The music patterns when a file doesn't say: music\* and data\global\music\* (Diablo II)
const std::vector<std::string> &PlaylistDefaultMusic();

struct PlaylistReadOptions
{
	std::string label;
	bool bMap = false;
	bool bFromDisk = false;
};

// Limits on what a map's file may contain
#define PLAYLIST_MAP_MAX_FILE_SIZE (64 * 1024)
#define PLAYLIST_MAP_MAX_PLAYLISTS 256
#define PLAYLIST_MAP_MAX_TRACKS 2048
#define PLAYLIST_MAP_MAX_TRANSITIONS 1024
#define PLAYLIST_MAP_MAX_MUSIC_PATTERNS 64

// Limits on any file
#define PLAYLIST_MAX_PATH 260
#define PLAYLIST_MAX_TIMES 1000000
#define PLAYLIST_MAX_SECONDS 3600

// Returns NULL if the file has errors. Warnings and errors go into diagnostics.
std::shared_ptr<const PlaylistFile> ReadPlaylistFile(const std::string &text, const PlaylistReadOptions &options, PlaylistDiagnostics &diagnostics);

// Warnings for the names a map's file uses that neither it nor the base file defines (base may be NULL, for a map that doesn't inherit or a base that is missing). Transitions to those names are ignored while the map is loaded.
void CheckPlaylistCombination(const PlaylistFile &map, const PlaylistFile *lpBase, PlaylistDiagnostics &diagnostics);

// Warnings for patterns in the same table that are equally specific and can match the same file; for such a file, the alphabetically first pattern wins. Comparing every pair takes time, so the plugin only does this when the conflict actually happens.
void FindAmbiguousPlaylistPatterns(const PlaylistFile &file, PlaylistDiagnostics &diagnostics);

#endif // #ifndef PLAYLISTFILE_H
