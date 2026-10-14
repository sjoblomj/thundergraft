/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	Runs a list of events, such as the game starting music, through the real CPlaylistPlayer with a simulated audio engine, and describes what plays after each. This is tgplaylist simulate, and how the playlist logic is tested end to end.

	The events, one per line, with # starting a comment, and paths with spaces in quotes:

		start <file> [loop] [as <name>]   The game starts a stream of the file; streams are called s1, s2, ... unless named
		end-track [<stream>]              The track playing in the stream plays to its end
		end-visit [<stream>]              Every track of the stream's visit plays to its end
		stop [<stream>]                   The game ends the stream
		wait <seconds>                    Time passes
		length <file> <seconds>           How long a file plays; 120 seconds unless given
		missing <file>                    The file can't be found
		load-map <playlist.toml> [as <name>]   A map with this playlist file is loaded
		unload-map [<name>]               The map is unloaded; the last one loaded unless named

	Without a stream, events are about the music stream, or else the stream started last.

	Platform-independent, so it can be tested anywhere.
*/

#ifndef PLAYLISTSIMULATOR_H
#define PLAYLISTSIMULATOR_H

#include <stdint.h>
#include <functional>
#include <memory>
#include <string>

#include "PlaylistFile.h"

struct PlaylistSimulation
{
	// The base file; may be NULL, for maps only
	std::shared_ptr<const PlaylistFile> base;
	// The events, and what to call them in messages
	std::string events;
	std::string eventsLabel = "events";
	// For shuffles
	uint64_t nSeed = 1;
	// Reads the playlist files of load-map events. Returns false if the file can't be read.
	std::function<bool(const std::string &path, std::string *lpText)> readFile;
};

// Returns false if an event couldn't be understood; the output says which
bool RunPlaylistSimulation(const PlaylistSimulation &simulation, std::string *lpOutput);

#endif // #ifndef PLAYLISTSIMULATOR_H
