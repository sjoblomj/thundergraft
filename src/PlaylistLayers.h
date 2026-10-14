/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	The playlist files in use, as layers: the base file at the bottom, and the playlist files of the StarCraft maps that are loaded on top of it, the most recently loaded one in use.

	While a map is loaded, playlist names are looked up in the map's file first, then in the base file's, and the map's top-level transitions come between the current playlist's and the base file's. A map's file with inherit = false hides the base file completely.

	The layers also keep how far each playlist has got (its PlaylistPass). A map gets passes of its own, copied from the layer below when a playlist is first played on it, so that when the map is unloaded the base file's playlists continue where they were before the map.

	Platform-independent, so it can be tested anywhere.
*/

#ifndef PLAYLISTLAYERS_H
#define PLAYLISTLAYERS_H

#include <stdint.h>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "PlaylistFile.h"
#include "PlaylistVisit.h"

// Identifies a stream: the game's file handle
typedef uintptr_t PlaylistStreamId;

// Where problems found while playing go, and, if wanted, a trace of every transition
class IPlaylistLog
{
public:
	virtual ~IPlaylistLog() {}

	virtual void Warning(const PlaylistDiagnostic &diagnostic) = 0;

	// Tracing every transition is opt-in, as it grows without end
	virtual bool TraceEnabled() = 0;
	virtual void Trace(const std::string &text) = 0;

	// How a stream is called in the trace
	virtual std::string StreamName(PlaylistStreamId id);
};

// A playlist and the file that defines it, which the reference keeps alive
struct PlaylistRef
{
	std::shared_ptr<const PlaylistFile> file;
	const Playlist *playlist = NULL;

	explicit operator bool() const
	{ return playlist != NULL; }

	bool operator==(const PlaylistRef &other) const
	{ return playlist == other.playlist; }

	bool operator!=(const PlaylistRef &other) const
	{ return playlist != other.playlist; }

	void Reset()
	{
		file.reset();
		playlist = NULL;
	}

	// "terranlist", or "none"
	std::string Name() const;
};

// The transition that applies to a file, and where it was found
struct PlaylistMatch
{
	const PlaylistTransition *transition = NULL;
	const PlaylistFile *file = NULL;
	// "playlist \"dark_terran\": on", or "[on]"
	std::string table;

	// "[on] 'music\terran?.wav' (music\playlist.toml:5)"
	std::string Describe() const;
};

class CPlaylistLayers
{
public:
	struct Layer
	{
		uintptr_t tag;
		uint64_t nId;
		std::shared_ptr<const PlaylistFile> file;
	};

private:
	std::shared_ptr<const PlaylistFile> m_base;
	// The maps' layers, most recently loaded last
	std::vector<Layer> m_maps;
	uint64_t m_nNextId;

	// The passes of each layer: 0 for the base file, and the layer's ID for a map
	std::map<uint64_t, std::map<const Playlist *, PlaylistPass>> m_passes;

	// Equally specific patterns that have already been warned about
	std::set<std::string> m_warnedTies;

	bool FindInTable(const std::vector<PlaylistTransition> &transitions, const std::string &name, const PlaylistFile &file, const std::string &table, PlaylistMatch *lpMatch, IPlaylistLog *lpLog);

public:
	CPlaylistLayers();

	void SetBase(std::shared_ptr<const PlaylistFile> base);
	const std::shared_ptr<const PlaylistFile> &Base() const
	{ return m_base; }

	// No playlist file at all
	bool Empty() const;

	// Whether the base file lets maps' playlist files be used
	bool MapsAllowed() const
	{ return !m_base || m_base->bMapPlaylists; }

	// Returns false if maps' files aren't allowed, or the tag is already in use
	bool PushMap(uintptr_t tag, std::shared_ptr<const PlaylistFile> file);
	bool RemoveMap(uintptr_t tag, Layer *lpRemoved);
	bool HasMap(uintptr_t tag) const;
	// The layer in use, or NULL
	const Layer *TopMap() const;
	// The IDs of every map that is loaded, in use or not
	std::vector<uint64_t> MapIds() const;

	// The files in use, in the order names are looked up in
	std::vector<std::shared_ptr<const PlaylistFile>> Effective() const;
	bool IsEffective(const PlaylistFile *lpFile) const;

	// An empty reference if no file in use defines it
	PlaylistRef Find(const std::string &name) const;

	// Whether a file the game opens is music. The name must be normalized.
	bool IsMusic(const std::string &name) const;

	// The transition for a stream of the file starting, with the current playlist's transitions first. The name must be normalized. Warns, once each, about equally specific patterns that both match.
	bool FindTransition(const std::string &name, const PlaylistRef &current, PlaylistMatch *lpMatch, IPlaylistLog *lpLog);

	// The playlist's pass on the layer in use
	PlaylistPass &Pass(const Playlist &playlist);
};

#endif // #ifndef PLAYLISTLAYERS_H
