/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

#include "PlaylistLoader.h"
#include "SfplsConverter.h"

std::shared_ptr<const PlaylistFile> ReadAnyPlaylistFile(const std::string &text, bool bSfpls, const PlaylistReadOptions &options, PlaylistDiagnostics &diagnostics)
{
	if (!bSfpls)
		return ReadPlaylistFile(text, options, diagnostics);

	std::string toml;
	if (!ConvertSfplsPlaylist(text, options.label, &toml, diagnostics))
		return nullptr;

	PlaylistReadOptions converted = options;
	converted.label = options.label + " (converted)";
	return ReadPlaylistFile(toml, converted, diagnostics);
}

std::shared_ptr<const PlaylistFile> LoadBasePlaylist(IPlaylistSources &sources, PlaylistDiagnostics &diagnostics, bool *lpbFound)
{
	*lpbFound = false;

	struct Candidate
	{
		const char *lpszName;
		bool bSfpls;
		bool bDisk;
	};
	const Candidate candidates[] =
	{
		{ PLAYLIST_TOML_NAME, false, false },
		{ PLAYLIST_TXT_NAME, true, false },
		{ PLAYLIST_TOML_NAME, false, true },
		{ PLAYLIST_TXT_NAME, true, true },
	};

	for (const Candidate &candidate : candidates)
	{
		std::string text, label = candidate.lpszName;
		bool bFound = candidate.bDisk ?
			sources.ReadFromDisk(candidate.lpszName, PLAYLIST_MAX_FILE_SIZE, &text, &label) :
			sources.ReadFromArchives(candidate.lpszName, PLAYLIST_MAX_FILE_SIZE, &text);
		if (!bFound)
			continue;

		*lpbFound = true;
		if (text.empty())
		{
			diagnostics.Error(label, 0, "the file is empty, or bigger than " + std::to_string(PLAYLIST_MAX_FILE_SIZE / (1024 * 1024)) + " MB");
			return nullptr;
		}

		PlaylistReadOptions options;
		options.label = label;
		options.bFromDisk = candidate.bDisk;
		return ReadAnyPlaylistFile(text, candidate.bSfpls, options, diagnostics);
	}

	return nullptr;
}
