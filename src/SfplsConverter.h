/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	Converts the music\playlist.txt files of ShadowFlare's Playlist Plugin to ThunderGraft's TOML playlist format, so that existing mods keep working. The plugin converts them in memory; the tgplaylist tool writes the result out for modders to extend.

	There are two formats. A plain list of files replaces all music, each file the game opens in its music directory with the next file in the list. A file starting with <SFPlaylist1> maps files the game plays to named lists:

		<SFPlaylist1>
		music\terran1.wav = terran

		<terran>
		music\mymusic1.wav
		music\mymusic2.wav

	Platform-independent, so it can be tested anywhere.
*/

#ifndef SFPLSCONVERTER_H
#define SFPLSCONVERTER_H

#include <string>

#include "PlaylistDiagnostics.h"

// Whether the first line that isn't blank is <SFPlaylist1>
bool IsSfplsListFormat(const std::string &text);

// Converts either format. Returns false, with the problems in diagnostics, if it can't be converted cleanly; a converted file with errors would only half work, so it isn't used. Warnings (such as a renamed list) don't stop the conversion.
bool ConvertSfplsPlaylist(const std::string &text, const std::string &label, std::string *lpToml, PlaylistDiagnostics &diagnostics);

#endif // #ifndef SFPLSCONVERTER_H
