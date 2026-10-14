/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	File names and file patterns, as the games open files and as playlist files name them.

	Names are compared the way Storm compares them in archives: case-insensitively (ASCII only), with / and \ the same. Normalizing a name lowercases it and turns / into \, so normalized names can be compared directly.

	Patterns may contain * (any characters, including \ and none at all) and ? (any one character). When several patterns match a file, the most specific one wins: a pattern without wildcards beats any pattern with them, and otherwise the one with the most characters other than * and ? wins.

	Platform-independent, so it can be tested anywhere.
*/

#ifndef PLAYLISTPATH_H
#define PLAYLISTPATH_H

#include <string>

// Lowercases ASCII letters and turns / into \.
std::string NormalizePlaylistName(const std::string &name);

// Turns / into \, as Storm expects, keeping the case
std::string PlaylistPathForStorm(const std::string &path);

bool HasPlaylistWildcards(const std::string &pattern);

// Both must be normalized
bool MatchPlaylistPattern(const std::string &pattern, const std::string &name);

// The number of characters other than * and ?
size_t PlaylistPatternSpecificity(const std::string &pattern);

// Positive if pattern a is more specific than b, negative if less, 0 if they are equally specific. Doesn't look at whether they match the same files.
int ComparePlaylistPatterns(const std::string &a, const std::string &b);

// Whether some file name matches both patterns. Both must be normalized.
bool PlaylistPatternsOverlap(const std::string &a, const std::string &b);

// A tab, newline or other character below 0x20, or DEL
bool HasPlaylistControlCharacter(const std::string &path);

// A path from the root of a drive or share, rather than one relative to the game or its archives: C:\x, C:x, \x, \\server\x
bool IsAbsolutePlaylistPath(const std::string &path);

// Whether a component of the path is ".."
bool HasPlaylistParentReference(const std::string &path);

#endif // #ifndef PLAYLISTPATH_H
