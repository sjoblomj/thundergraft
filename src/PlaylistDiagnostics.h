/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	Problems found in playlist files. Errors keep a file from being used; warnings don't. The plugin writes them to ThunderGraft.log and the tgplaylist tool prints them, in the same words: "<file>:<line>: error: <message>".

	Platform-independent, so it can be tested anywhere.
*/

#ifndef PLAYLISTDIAGNOSTICS_H
#define PLAYLISTDIAGNOSTICS_H

#include <string>
#include <vector>

enum PlaylistSeverity
{
	PLAYLIST_ERROR,
	PLAYLIST_WARNING
};

struct PlaylistDiagnostic
{
	PlaylistSeverity severity;
	// The file it's about, as it is called in messages
	std::string file;
	// 0 if there is no particular line
	unsigned nLine;
	std::string message;
};

// "<file>:<line>: error: <message>", without the line if there is none
std::string FormatPlaylistDiagnostic(const PlaylistDiagnostic &diagnostic);

class PlaylistDiagnostics
{
private:
	std::vector<PlaylistDiagnostic> m_diagnostics;

public:
	void Error(const std::string &file, unsigned nLine, const std::string &message)
	{ m_diagnostics.push_back(PlaylistDiagnostic{PLAYLIST_ERROR, file, nLine, message}); }

	void Warning(const std::string &file, unsigned nLine, const std::string &message)
	{ m_diagnostics.push_back(PlaylistDiagnostic{PLAYLIST_WARNING, file, nLine, message}); }

	void Add(const PlaylistDiagnostic &diagnostic)
	{ m_diagnostics.push_back(diagnostic); }

	const std::vector<PlaylistDiagnostic> &All() const
	{ return m_diagnostics; }

	bool HasErrors() const;
	size_t ErrorCount() const;
	size_t WarningCount() const;

	// Every diagnostic, formatted, one per line
	std::string Format() const;
};

// Quotes a path or pattern for a message, the way playlist files write them: 'music\x.wav'
std::string QuotePlaylistPath(const std::string &path);

#endif // #ifndef PLAYLISTDIAGNOSTICS_H
