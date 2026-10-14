/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	Which tracks a playlist plays, and in what order.

	Playing a playlist in a stream is a visit. With play = "one", a visit plays the playlist's next entry (a track, as many times in a row as its times says); with play = "all", it plays the whole list, the playlist's times times. Either way the entries are dealt from passes: a pass is every entry once, in order or shuffled, and a shuffled pass never starts with the entry the previous pass ended with.

	A play = "one" playlist continues its pass from one visit to the next, which is what makes every track play before any repeats when the game asks for new music after each track. That position is a PlaylistPass, kept for the rest of the session. A play = "all" visit starts a new pass of its own each time.

	Platform-independent, so it can be tested anywhere.
*/

#ifndef PLAYLISTVISIT_H
#define PLAYLISTVISIT_H

#include <stdint.h>
#include <vector>

#include "PlaylistFile.h"

// A small random number generator, so that shuffles can be repeated from a seed in tests and the simulator
class CPlaylistRandom
{
private:
	uint64_t m_nState;

public:
	explicit CPlaylistRandom(uint64_t nSeed = 0) : m_nState(nSeed) {}

	void Seed(uint64_t nSeed)
	{ m_nState = nSeed; }

	uint64_t Next();
	// From 0 to nBound - 1; nBound must not be 0
	size_t Below(size_t nBound);
};

// How far a playlist has got through its entries
struct PlaylistPass
{
	// The entries of the current pass, as indices into the playlist's tracks
	std::vector<unsigned> order;
	// The next entry of order to deal
	size_t nNext = 0;
	// The entry dealt last, which the next pass mustn't start with; -1 for none
	int nLastEntry = -1;

	// Deals the next entry, starting a new pass when this one is used up
	unsigned Take(const Playlist &playlist, CPlaylistRandom &random);
};

// Makes a pass: every entry once, in order or shuffled, not starting with nLastEntry if that can be avoided
std::vector<unsigned> MakePlaylistPass(const Playlist &playlist, int nLastEntry, CPlaylistRandom &random);

class CPlaylistVisit
{
private:
	const Playlist *m_lpPlaylist;
	// The stream loops, so the visit never ends
	bool m_bLoop;

	// play = "all": this visit's own pass, and how many passes it has finished
	std::vector<unsigned> m_order;
	size_t m_nIndex;
	unsigned m_nPassesDone;

	// The entry being played, and how many of its times have been handed out
	int m_nEntry;
	unsigned m_nPlays;

public:
	CPlaylistVisit();

	// Starts a visit of the playlist, which must outlive it. The pass is the playlist's.
	void Begin(const Playlist &playlist, bool bLoop, PlaylistPass &pass, CPlaylistRandom &random);

	const Playlist *GetPlaylist() const
	{ return m_lpPlaylist; }

	// The track to play next, or -1 once the visit has ended
	int NextTrack(PlaylistPass &pass, CPlaylistRandom &random);

	// The track handed out last couldn't be played: drop the rest of that entry's times. With play = "one", the visit takes the next entry in its place.
	void SkipEntry(PlaylistPass &pass, CPlaylistRandom &random);

	// play = "one" in a looping stream: the track is opened looping, and plays until the visit is cut short
	bool LoopsTrack() const
	{ return m_bLoop && m_lpPlaylist && !m_lpPlaylist->bPlayAll; }
};

#endif // #ifndef PLAYLISTVISIT_H
