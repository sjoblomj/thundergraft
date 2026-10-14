/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

#include "PlaylistVisit.h"

#include <assert.h>
#include <utility>

uint64_t CPlaylistRandom::Next()
{
	// SplitMix64
	uint64_t z = (m_nState += 0x9E3779B97F4A7C15ULL);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	return z ^ (z >> 31);
}

size_t CPlaylistRandom::Below(size_t nBound)
{
	assert(nBound > 0);
	return (size_t)(Next() % nBound);
}

std::vector<unsigned> MakePlaylistPass(const Playlist &playlist, int nLastEntry, CPlaylistRandom &random)
{
	std::vector<unsigned> order(playlist.tracks.size());
	for (size_t iEntry = 0; iEntry < order.size(); iEntry++)
		order[iEntry] = (unsigned)iEntry;

	if (playlist.bShuffle && order.size() > 1)
	{
		// Fisher-Yates
		for (size_t iEntry = order.size() - 1; iEntry > 0; iEntry--)
			std::swap(order[iEntry], order[random.Below(iEntry + 1)]);

		// Like a reshuffled deck, except that the card on top of the old deck can't be the first one dealt from the new
		if ((int)order[0] == nLastEntry)
			std::swap(order[0], order[1 + random.Below(order.size() - 1)]);
	}

	return order;
}

unsigned PlaylistPass::Take(const Playlist &playlist, CPlaylistRandom &random)
{
	// The playlist can't change, but guard against an order made for another one anyway
	if (nNext >= order.size() || order.size() != playlist.tracks.size())
	{
		order = MakePlaylistPass(playlist, nLastEntry, random);
		nNext = 0;
	}

	unsigned nEntry = order[nNext++];
	nLastEntry = (int)nEntry;

	return nEntry;
}

CPlaylistVisit::CPlaylistVisit()
{
	m_lpPlaylist = NULL;
	m_bLoop = false;
	m_nIndex = 0;
	m_nPassesDone = 0;
	m_nEntry = -1;
	m_nPlays = 0;
}

void CPlaylistVisit::Begin(const Playlist &playlist, bool bLoop, PlaylistPass &pass, CPlaylistRandom &random)
{
	m_lpPlaylist = &playlist;
	m_bLoop = bLoop;
	m_nPassesDone = 0;
	m_nPlays = 0;

	if (playlist.bPlayAll)
	{
		m_order = MakePlaylistPass(playlist, pass.nLastEntry, random);
		m_nIndex = 0;
		m_nEntry = -1;
	}
	else
	{
		m_order.clear();
		m_nEntry = (int)pass.Take(playlist, random);
	}
}

int CPlaylistVisit::NextTrack(PlaylistPass &pass, CPlaylistRandom &random)
{
	if (!m_lpPlaylist || m_lpPlaylist->tracks.empty())
		return -1;

	const Playlist &playlist = *m_lpPlaylist;

	if (!playlist.bPlayAll)
	{
		if (m_nEntry < 0)
			return -1;

		// A looping stream plays the track for as long as the visit lasts. It is opened looping, so this is only asked again if it couldn't be played.
		if (m_bLoop || m_nPlays < playlist.tracks[m_nEntry].nTimes)
		{
			m_nPlays++;
			return m_nEntry;
		}

		return -1;
	}

	for (;;)
	{
		if (m_nEntry >= 0 && m_nPlays < playlist.tracks[m_nEntry].nTimes)
		{
			m_nPlays++;
			return m_nEntry;
		}

		if (m_nIndex >= m_order.size())
		{
			// The pass is over. Each of the visit's repetitions is a pass of its own.
			m_nPassesDone++;
			if (!m_bLoop && !playlist.bForever && m_nPassesDone >= playlist.nTimes)
			{
				m_nEntry = -1;
				return -1;
			}

			m_order = MakePlaylistPass(playlist, pass.nLastEntry, random);
			m_nIndex = 0;
		}

		m_nEntry = (int)m_order[m_nIndex++];
		m_nPlays = 0;
		pass.nLastEntry = m_nEntry;
	}
}

void CPlaylistVisit::SkipEntry(PlaylistPass &pass, CPlaylistRandom &random)
{
	if (!m_lpPlaylist || m_nEntry < 0)
		return;

	if (m_lpPlaylist->bPlayAll)
		m_nPlays = m_lpPlaylist->tracks[m_nEntry].nTimes;
	else
	{
		m_nEntry = (int)pass.Take(*m_lpPlaylist, random);
		m_nPlays = 0;
	}
}
