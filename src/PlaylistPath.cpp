/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

#include "PlaylistPath.h"

#include <vector>

namespace
{
	char FoldCase(char c)
	{
		return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
	}

	bool IsSeparator(char c)
	{
		return c == '\\' || c == '/';
	}

	// Whether two pattern characters, neither of them *, can match the same character
	bool CharactersCompatible(char a, char b)
	{
		return a == '?' || b == '?' || a == b;
	}
}

std::string NormalizePlaylistName(const std::string &name)
{
	std::string normalized(name);
	for (char &c : normalized)
		c = (c == '/') ? '\\' : FoldCase(c);

	return normalized;
}

std::string PlaylistPathForStorm(const std::string &path)
{
	std::string converted(path);
	for (char &c : converted)
	{
		if (c == '/')
			c = '\\';
	}

	return converted;
}

bool HasPlaylistWildcards(const std::string &pattern)
{
	return pattern.find_first_of("*?") != std::string::npos;
}

bool MatchPlaylistPattern(const std::string &pattern, const std::string &name)
{
	// The usual backtracking glob match: on a mismatch, go back to the last * and let it take one more character
	size_t iPattern = 0, iName = 0;
	size_t iStar = std::string::npos, iStarName = 0;

	while (iName < name.size())
	{
		if (iPattern < pattern.size() && (pattern[iPattern] == '?' || pattern[iPattern] == name[iName]))
		{
			iPattern++;
			iName++;
		}
		else if (iPattern < pattern.size() && pattern[iPattern] == '*')
		{
			iStar = iPattern++;
			iStarName = iName;
		}
		else if (iStar != std::string::npos)
		{
			iPattern = iStar + 1;
			iName = ++iStarName;
		}
		else
			return false;
	}

	while (iPattern < pattern.size() && pattern[iPattern] == '*')
		iPattern++;

	return iPattern == pattern.size();
}

size_t PlaylistPatternSpecificity(const std::string &pattern)
{
	size_t nSpecificity = 0;
	for (char c : pattern)
	{
		if (c != '*' && c != '?')
			nSpecificity++;
	}

	return nSpecificity;
}

int ComparePlaylistPatterns(const std::string &a, const std::string &b)
{
	bool bExactA = !HasPlaylistWildcards(a), bExactB = !HasPlaylistWildcards(b);
	if (bExactA != bExactB)
		return bExactA ? 1 : -1;

	size_t nA = PlaylistPatternSpecificity(a), nB = PlaylistPatternSpecificity(b);
	if (nA != nB)
		return nA > nB ? 1 : -1;

	return 0;
}

bool PlaylistPatternsOverlap(const std::string &a, const std::string &b)
{
	// reachable[i][j]: some string can take pattern a from position i and pattern b from position j to their ends together. Filled from the ends backwards.
	size_t nA = a.size(), nB = b.size();
	std::vector<std::vector<char>> reachable(nA + 1, std::vector<char>(nB + 1, 0));

	for (size_t i = nA + 1; i-- > 0;)
	{
		for (size_t j = nB + 1; j-- > 0;)
		{
			bool bReachable;
			if (i == nA && j == nB)
				bReachable = true;
			else if (i < nA && a[i] == '*')
				// The * matches nothing, or it takes the character b produces next (if b is at a * too, that * matching nothing covers it)
				bReachable = reachable[i + 1][j] || (j < nB && reachable[i][j + 1]);
			else if (j < nB && b[j] == '*')
				bReachable = reachable[i][j + 1] || (i < nA && reachable[i + 1][j]);
			else if (i < nA && j < nB)
				bReachable = CharactersCompatible(a[i], b[j]) && reachable[i + 1][j + 1];
			else
				bReachable = false;

			reachable[i][j] = bReachable;
		}
	}

	return reachable[0][0] != 0;
}

bool HasPlaylistControlCharacter(const std::string &path)
{
	for (char c : path)
	{
		unsigned char uc = (unsigned char)c;
		if (uc < 0x20 || uc == 0x7F)
			return true;
	}

	return false;
}

bool IsAbsolutePlaylistPath(const std::string &path)
{
	if (!path.empty() && IsSeparator(path[0]))
		return true;

	// A drive letter, with or without a separator after the colon
	return path.size() >= 2 && path[1] == ':' && FoldCase(path[0]) >= 'a' && FoldCase(path[0]) <= 'z';
}

bool HasPlaylistParentReference(const std::string &path)
{
	size_t iStart = 0;
	while (iStart <= path.size())
	{
		size_t iEnd = iStart;
		while (iEnd < path.size() && !IsSeparator(path[iEnd]))
			iEnd++;

		if (path.compare(iStart, iEnd - iStart, "..") == 0)
			return true;

		iStart = iEnd + 1;
	}

	return false;
}
