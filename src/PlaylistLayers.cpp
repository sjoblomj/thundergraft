/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

#include "PlaylistLayers.h"
#include "PlaylistPath.h"

#include <stdio.h>

std::string IPlaylistLog::StreamName(PlaylistStreamId id)
{
	char szName[32];
	snprintf(szName, sizeof(szName), "stream %p", (void *)id);
	return szName;
}

std::string PlaylistRef::Name() const
{
	return playlist ? "\"" + playlist->name + "\"" : "none";
}

std::string PlaylistMatch::Describe() const
{
	if (!transition || !file)
		return "no transition";

	return table + " " + QuotePlaylistPath(transition->written) + " (" + file->label + ":" + std::to_string(transition->nLine) + ")";
}

CPlaylistLayers::CPlaylistLayers()
{
	m_nNextId = 1;
}

void CPlaylistLayers::SetBase(std::shared_ptr<const PlaylistFile> base)
{
	m_base = base;
	m_passes.erase(0);
}

bool CPlaylistLayers::Empty() const
{
	return !m_base && m_maps.empty();
}

bool CPlaylistLayers::PushMap(uintptr_t tag, std::shared_ptr<const PlaylistFile> file)
{
	if (!file || !MapsAllowed() || HasMap(tag))
		return false;

	m_maps.push_back(Layer{tag, m_nNextId++, file});
	return true;
}

bool CPlaylistLayers::RemoveMap(uintptr_t tag, Layer *lpRemoved)
{
	for (std::vector<Layer>::iterator itLayer = m_maps.begin(); itLayer != m_maps.end(); itLayer++)
	{
		if (itLayer->tag != tag)
			continue;

		if (lpRemoved)
			*lpRemoved = *itLayer;

		m_passes.erase(itLayer->nId);
		m_maps.erase(itLayer);
		return true;
	}

	return false;
}

bool CPlaylistLayers::HasMap(uintptr_t tag) const
{
	for (const Layer &layer : m_maps)
	{
		if (layer.tag == tag)
			return true;
	}

	return false;
}

const CPlaylistLayers::Layer *CPlaylistLayers::TopMap() const
{
	return (!m_maps.empty() && MapsAllowed()) ? &m_maps.back() : NULL;
}

std::vector<uint64_t> CPlaylistLayers::MapIds() const
{
	std::vector<uint64_t> ids;
	for (const Layer &layer : m_maps)
		ids.push_back(layer.nId);

	return ids;
}

std::vector<std::shared_ptr<const PlaylistFile>> CPlaylistLayers::Effective() const
{
	std::vector<std::shared_ptr<const PlaylistFile>> files;
	const Layer *lpTop = TopMap();
	if (lpTop)
		files.push_back(lpTop->file);
	if (m_base && (!lpTop || lpTop->file->bInherit))
		files.push_back(m_base);

	return files;
}

bool CPlaylistLayers::IsEffective(const PlaylistFile *lpFile) const
{
	for (const std::shared_ptr<const PlaylistFile> &file : Effective())
	{
		if (file.get() == lpFile)
			return true;
	}

	return false;
}

PlaylistRef CPlaylistLayers::Find(const std::string &name) const
{
	PlaylistRef ref;
	for (const std::shared_ptr<const PlaylistFile> &file : Effective())
	{
		if (const Playlist *lpPlaylist = file->Find(name))
		{
			ref.file = file;
			ref.playlist = lpPlaylist;
			break;
		}
	}

	return ref;
}

bool CPlaylistLayers::IsMusic(const std::string &name) const
{
	const std::vector<std::string> *lpMusic = &PlaylistDefaultMusic();
	for (const std::shared_ptr<const PlaylistFile> &file : Effective())
	{
		if (file->bHasMusic)
		{
			lpMusic = &file->music;
			break;
		}
	}

	for (const std::string &pattern : *lpMusic)
	{
		if (MatchPlaylistPattern(pattern, name))
			return true;
	}

	return false;
}

bool CPlaylistLayers::FindInTable(const std::vector<PlaylistTransition> &transitions, const std::string &name, const PlaylistFile &file, const std::string &table, PlaylistMatch *lpMatch, IPlaylistLog *lpLog)
{
	const PlaylistTransition *lpBest = NULL, *lpTie = NULL;
	for (const PlaylistTransition &transition : transitions)
	{
		if (!MatchPlaylistPattern(transition.pattern, name))
			continue;

		int nComparison = lpBest ? ComparePlaylistPatterns(transition.pattern, lpBest->pattern) : 1;
		if (nComparison > 0)
		{
			lpBest = &transition;
			lpTie = NULL;
		}
		else if (nComparison == 0)
		{
			// Equally specific: the alphabetically first wins
			if (transition.pattern < lpBest->pattern)
			{
				lpTie = lpBest;
				lpBest = &transition;
			}
			else if (!lpTie || transition.pattern < lpTie->pattern)
				lpTie = &transition;
		}
	}

	if (!lpBest)
		return false;

	if (lpTie && lpLog)
	{
		std::string key = file.label + "\n" + table + "\n" + lpBest->pattern + "\n" + lpTie->pattern;
		if (m_warnedTies.insert(key).second)
			lpLog->Warning(PlaylistDiagnostic{PLAYLIST_WARNING, file.label, lpTie->nLine, table + ": " + QuotePlaylistPath(lpTie->written) + " and " + QuotePlaylistPath(lpBest->written) + " (line " + std::to_string(lpBest->nLine) + ") both match " + QuotePlaylistPath(name) + " and are equally specific; " + QuotePlaylistPath(lpBest->written) + " wins, as it comes first alphabetically"});
	}

	lpMatch->transition = lpBest;
	lpMatch->file = &file;
	lpMatch->table = table;
	return true;
}

bool CPlaylistLayers::FindTransition(const std::string &name, const PlaylistRef &current, PlaylistMatch *lpMatch, IPlaylistLog *lpLog)
{
	// 1. The current playlist's own transitions, if its file is in use
	if (current && IsEffective(current.file.get()) &&
		FindInTable(current.playlist->on, name, *current.file, "playlist \"" + current.playlist->name + "\": on", lpMatch, lpLog))
		return true;

	// 2. The map's top-level transitions, then 3. the base file's
	for (const std::shared_ptr<const PlaylistFile> &file : Effective())
	{
		if (FindInTable(file->on, name, *file, "[on]", lpMatch, lpLog))
			return true;
	}

	return false;
}

PlaylistPass &CPlaylistLayers::Pass(const Playlist &playlist)
{
	const Layer *lpTop = TopMap();
	uint64_t nContext = lpTop ? lpTop->nId : 0;

	std::map<const Playlist *, PlaylistPass> &passes = m_passes[nContext];
	std::map<const Playlist *, PlaylistPass>::iterator itPass = passes.find(&playlist);
	if (itPass != passes.end())
		return itPass->second;

	// Start where the layers below have got to, without changing theirs
	PlaylistPass pass;
	for (size_t iLayer = m_maps.size() + 1; iLayer-- > 0;)
	{
		uint64_t nBelow = iLayer ? m_maps[iLayer - 1].nId : 0;
		if (nBelow == nContext || !m_passes.count(nBelow))
			continue;

		std::map<const Playlist *, PlaylistPass> &below = m_passes[nBelow];
		std::map<const Playlist *, PlaylistPass>::iterator itBelow = below.find(&playlist);
		if (itBelow != below.end())
		{
			pass = itBelow->second;
			break;
		}
	}

	return passes[&playlist] = pass;
}
