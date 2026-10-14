/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	Finds and reads the base playlist file, in this order:

	1. music\playlist.toml in the game's archives
	2. music\playlist.txt in the archives, converted (see SfplsConverter.h)
	3. music\playlist.toml in the game's directory on disk
	4. music\playlist.txt on disk, converted

	The first one found is used, even if it has errors: a mod's broken playlist doesn't silently give way to another.

	Platform-independent, so it can be tested anywhere; the plugin supplies the archives and the disk.
*/

#ifndef PLAYLISTLOADER_H
#define PLAYLISTLOADER_H

#include <memory>
#include <string>

#include "PlaylistDiagnostics.h"
#include "PlaylistFile.h"

// The base file may be at most this big; anything bigger isn't a playlist
#define PLAYLIST_MAX_FILE_SIZE (4 * 1024 * 1024)

#define PLAYLIST_TOML_NAME "music\\playlist.toml"
#define PLAYLIST_TXT_NAME "music\\playlist.txt"

class IPlaylistSources
{
public:
	virtual ~IPlaylistSources() {}

	// Reads a file from the game's archives only, not from disk. False if no archive has it. Files bigger than nMaxSize are reported as found, and lpText left empty.
	virtual bool ReadFromArchives(const std::string &name, size_t nMaxSize, std::string *lpText) = 0;
	// Reads a file from the game's directory. lpLabel is what to call it in messages, such as its full path.
	virtual bool ReadFromDisk(const std::string &name, size_t nMaxSize, std::string *lpText, std::string *lpLabel) = 0;
};

// Returns NULL if there is no playlist file, or if the one found has errors; lpbFound tells the two apart
std::shared_ptr<const PlaylistFile> LoadBasePlaylist(IPlaylistSources &sources, PlaylistDiagnostics &diagnostics, bool *lpbFound);

// Reads a playlist file that may be either format: TOML, or, if bSfpls, a playlist.txt to convert first
std::shared_ptr<const PlaylistFile> ReadAnyPlaylistFile(const std::string &text, bool bSfpls, const PlaylistReadOptions &options, PlaylistDiagnostics &diagnostics);

#endif // #ifndef PLAYLISTLOADER_H
