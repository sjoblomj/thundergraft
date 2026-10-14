// Tests of the order playlists play their tracks in

#include <doctest.h>

#include "PlaylistVisit.h"

#include <algorithm>
#include <set>

namespace
{
	Playlist MakePlaylist(size_t nTracks, bool bShuffle = false, bool bPlayAll = false)
	{
		Playlist playlist;
		playlist.name = "p";
		playlist.bShuffle = bShuffle;
		playlist.bPlayAll = bPlayAll;
		for (size_t iTrack = 0; iTrack < nTracks; iTrack++)
		{
			PlaylistTrack track;
			track.file = "t" + std::to_string(iTrack);
			playlist.tracks.push_back(track);
		}
		return playlist;
	}

	// Every track one visit plays
	std::vector<int> PlayVisit(const Playlist &playlist, PlaylistPass &pass, CPlaylistRandom &random, bool bLoop = false, size_t nMax = 1000)
	{
		CPlaylistVisit visit;
		visit.Begin(playlist, bLoop, pass, random);

		std::vector<int> tracks;
		for (int nTrack = visit.NextTrack(pass, random); nTrack >= 0 && tracks.size() < nMax; nTrack = visit.NextTrack(pass, random))
			tracks.push_back(nTrack);
		return tracks;
	}
}

TEST_CASE("The random numbers repeat from a seed")
{
	CPlaylistRandom a(42), b(42), c(43);
	uint64_t nA = a.Next();
	CHECK(nA == b.Next());
	CHECK(nA != c.Next());

	for (int i = 0; i < 1000; i++)
		CHECK(a.Below(7) < 7);
}

TEST_CASE("play = one plays one track per visit, continuing through the list")
{
	Playlist playlist = MakePlaylist(3);
	PlaylistPass pass;
	CPlaylistRandom random(1);

	CHECK(PlayVisit(playlist, pass, random) == std::vector<int>{0});
	CHECK(PlayVisit(playlist, pass, random) == std::vector<int>{1});
	CHECK(PlayVisit(playlist, pass, random) == std::vector<int>{2});
	// A new pass
	CHECK(PlayVisit(playlist, pass, random) == std::vector<int>{0});
}

TEST_CASE("A track's times plays it that many times in a row, in one visit")
{
	Playlist playlist = MakePlaylist(2);
	playlist.tracks[1].nTimes = 3;
	PlaylistPass pass;
	CPlaylistRandom random(1);

	CHECK(PlayVisit(playlist, pass, random) == std::vector<int>{0});
	CHECK(PlayVisit(playlist, pass, random) == std::vector<int>{1, 1, 1});

	SUBCASE("With play = all too")
	{
		playlist.bPlayAll = true;
		CHECK(PlayVisit(playlist, pass, random) == std::vector<int>{0, 1, 1, 1});
	}
}

TEST_CASE("play = all plays the whole list, times times, each a new pass")
{
	Playlist playlist = MakePlaylist(3, false, true);
	PlaylistPass pass;
	CPlaylistRandom random(1);

	CHECK(PlayVisit(playlist, pass, random) == std::vector<int>{0, 1, 2});
	// Every visit starts from the start
	CHECK(PlayVisit(playlist, pass, random) == std::vector<int>{0, 1, 2});

	playlist.nTimes = 2;
	CHECK(PlayVisit(playlist, pass, random) == std::vector<int>{0, 1, 2, 0, 1, 2});

	SUBCASE("forever never ends")
	{
		playlist.bForever = true;
		CHECK(PlayVisit(playlist, pass, random, false, 100).size() == 100);
	}
}

TEST_CASE("In a looping stream, a visit never ends")
{
	PlaylistPass pass;
	CPlaylistRandom random(1);

	SUBCASE("play = one loops its track")
	{
		Playlist playlist = MakePlaylist(3);
		CPlaylistVisit visit;
		visit.Begin(playlist, true, pass, random);
		CHECK(visit.LoopsTrack());
		CHECK(visit.NextTrack(pass, random) == 0);
		CHECK(visit.NextTrack(pass, random) == 0);
	}

	SUBCASE("play = all repeats its list")
	{
		Playlist playlist = MakePlaylist(2, false, true);
		CPlaylistVisit visit;
		visit.Begin(playlist, true, pass, random);
		CHECK_FALSE(visit.LoopsTrack());
		CHECK(PlayVisit(playlist, pass, random, true, 7) == std::vector<int>{0, 1, 0, 1, 0, 1, 0});
	}
}

TEST_CASE("A shuffled pass plays every track once")
{
	Playlist playlist = MakePlaylist(10, true);
	CPlaylistRandom random(7);

	for (int iPass = 0; iPass < 50; iPass++)
	{
		std::vector<unsigned> order = MakePlaylistPass(playlist, -1, random);
		std::vector<unsigned> sorted = order;
		std::sort(sorted.begin(), sorted.end());
		for (unsigned iTrack = 0; iTrack < 10; iTrack++)
			CHECK(sorted[iTrack] == iTrack);
	}

	SUBCASE("in different orders")
	{
		std::set<std::vector<unsigned>> orders;
		for (int iPass = 0; iPass < 20; iPass++)
			orders.insert(MakePlaylistPass(playlist, -1, random));
		CHECK(orders.size() > 15);
	}
}

TEST_CASE("A new shuffled pass never starts with the track the last one ended with")
{
	CPlaylistRandom random(3);

	for (size_t nTracks : { 2, 3, 5 })
	{
		CAPTURE(nTracks);
		Playlist playlist = MakePlaylist(nTracks, true);
		PlaylistPass pass;

		int nPrevious = -1;
		for (int iVisit = 0; iVisit < 300; iVisit++)
		{
			std::vector<int> tracks = PlayVisit(playlist, pass, random);
			REQUIRE(tracks.size() == 1);
			CHECK(tracks[0] != nPrevious);
			nPrevious = tracks[0];
		}
	}

	SUBCASE("Across the passes of a play = all visit, and from one visit to the next")
	{
		Playlist playlist = MakePlaylist(4, true, true);
		playlist.nTimes = 50;
		PlaylistPass pass;

		int nPrevious = -1;
		for (int iVisit = 0; iVisit < 5; iVisit++)
		{
			for (int nTrack : PlayVisit(playlist, pass, random))
			{
				CHECK(nTrack != nPrevious);
				nPrevious = nTrack;
			}
		}
	}

	SUBCASE("A single track can only repeat")
	{
		Playlist playlist = MakePlaylist(1, true);
		PlaylistPass pass;
		CHECK(PlayVisit(playlist, pass, random) == std::vector<int>{0});
		CHECK(PlayVisit(playlist, pass, random) == std::vector<int>{0});
	}
}

TEST_CASE("Every track plays before any repeats, visit by visit")
{
	Playlist playlist = MakePlaylist(6, true);
	PlaylistPass pass;
	CPlaylistRandom random(11);

	for (int iPass = 0; iPass < 10; iPass++)
	{
		std::set<int> played;
		for (int iVisit = 0; iVisit < 6; iVisit++)
			played.insert(PlayVisit(playlist, pass, random)[0]);
		CHECK(played.size() == 6);
	}
}

TEST_CASE("Skipping an entry")
{
	PlaylistPass pass;
	CPlaylistRandom random(1);

	SUBCASE("With play = one, the visit takes the next entry instead")
	{
		Playlist playlist = MakePlaylist(3);
		playlist.tracks[0].nTimes = 2;
		CPlaylistVisit visit;
		visit.Begin(playlist, false, pass, random);
		CHECK(visit.NextTrack(pass, random) == 0);
		visit.SkipEntry(pass, random);
		CHECK(visit.NextTrack(pass, random) == 1);
		CHECK(visit.NextTrack(pass, random) == -1);

		// The pass moved on past both
		CHECK(PlayVisit(playlist, pass, random) == std::vector<int>{2});
	}

	SUBCASE("With play = all, the rest of the entry's times are dropped")
	{
		Playlist playlist = MakePlaylist(2, false, true);
		playlist.tracks[0].nTimes = 3;
		CPlaylistVisit visit;
		visit.Begin(playlist, false, pass, random);
		CHECK(visit.NextTrack(pass, random) == 0);
		visit.SkipEntry(pass, random);
		CHECK(visit.NextTrack(pass, random) == 1);
		CHECK(visit.NextTrack(pass, random) == -1);
	}
}

TEST_CASE("A visit copied for later carries on from where it was")
{
	Playlist playlist = MakePlaylist(4, false, true);
	PlaylistPass pass;
	CPlaylistRandom random(1);

	CPlaylistVisit visit;
	visit.Begin(playlist, false, pass, random);
	CHECK(visit.NextTrack(pass, random) == 0);
	CHECK(visit.NextTrack(pass, random) == 1);

	CPlaylistVisit saved = visit;
	CHECK(visit.NextTrack(pass, random) == 2);

	CHECK(saved.NextTrack(pass, random) == 2);
	CHECK(saved.NextTrack(pass, random) == 3);
	CHECK(saved.NextTrack(pass, random) == -1);
}
