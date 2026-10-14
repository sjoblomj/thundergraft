/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

#include "PlaylistDiagnostics.h"

std::string FormatPlaylistDiagnostic(const PlaylistDiagnostic &diagnostic)
{
	std::string text = diagnostic.file;
	if (diagnostic.nLine)
		text += ":" + std::to_string(diagnostic.nLine);

	text += diagnostic.severity == PLAYLIST_ERROR ? ": error: " : ": warning: ";
	text += diagnostic.message;

	return text;
}

bool PlaylistDiagnostics::HasErrors() const
{
	return ErrorCount() > 0;
}

size_t PlaylistDiagnostics::ErrorCount() const
{
	size_t nErrors = 0;
	for (const PlaylistDiagnostic &diagnostic : m_diagnostics)
	{
		if (diagnostic.severity == PLAYLIST_ERROR)
			nErrors++;
	}

	return nErrors;
}

size_t PlaylistDiagnostics::WarningCount() const
{
	return m_diagnostics.size() - ErrorCount();
}

std::string PlaylistDiagnostics::Format() const
{
	std::string text;
	for (const PlaylistDiagnostic &diagnostic : m_diagnostics)
		text += FormatPlaylistDiagnostic(diagnostic) + "\n";

	return text;
}

std::string QuotePlaylistPath(const std::string &path)
{
	return "'" + path + "'";
}
