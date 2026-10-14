// Tests of playlists through CDdaPlayer, the DDA emulation: what the game sees of a stream a playlist plays in, and the Storm files the playlist's tracks are read from

#include <doctest.h>

#include "fakes/SharedPtrStrings.h"

#include "DdaPlayer.h"
#include "DdaConversions.h"
#include "PlaylistFile.h"
#include "StormFileNames.h"

#include "fakes/FakeAudioEngine.h"
#include "fakes/FakeDirectSoundBuffer.h"
#include "fakes/FakePlaylistFiles.h"
#include "fakes/FakeStorm.h"

#include <fstream>
#include <sstream>

namespace
{
	std::shared_ptr<const PlaylistFile> Parse(const std::string &text, bool bMap = false, bool bFromDisk = false)
	{
		PlaylistReadOptions options;
		options.label = "playlist.toml";
		options.bMap = bMap;
		options.bFromDisk = bFromDisk;
		PlaylistDiagnostics diagnostics;
		std::shared_ptr<const PlaylistFile> file = ReadPlaylistFile(text, options, diagnostics);
		INFO(diagnostics.Format());
		REQUIRE(file);
		return file;
	}

	const char *const PLAYLIST = R"(version = 1
[on]
'music\title.wav' = "title"
'music\terran?.wav' = "terran"
[playlist.title]
tracks = ['music\title.ogg']
[playlist.terran]
play = "all"
tracks = ['music\t1.ogg', 'music\t2.ogg']
)";

	struct PlaylistFixture
	{
		FakeAudioEngine engine;
		CStormFileNames names;
		FakePlaylistLog log;
		CDdaPlayer player;
		std::vector<std::unique_ptr<FakeDirectSoundBuffer>> buffers;

		PlaylistFixture()
		{
			FakeStorm::Reset();
			FakeStorm::AddNamedFile("music\\title.ogg", "title track");
			FakeStorm::AddNamedFile("music\\t1.ogg", "terran track 1");
			FakeStorm::AddNamedFile("music\\t2.ogg", "terran track 2");
			player.Initialize(FakeStorm::Functions(), &engine, &names, &log);
			player.SetBasePlaylist(Parse(PLAYLIST));
		}

		~PlaylistFixture()
		{
			player.StopAll();
		}

		// A file the game opens, by name, as the SFileOpenFile(Ex) hooks would record it
		HANDLE Open(const std::string &name, const std::string &contents = "the game's own music")
		{
			HANDLE hFile = FakeStorm::AddFile(contents);
			names.Set(hFile, name.c_str());
			return hFile;
		}

		BOOL Begin(HANDLE hFile, DWORD dwFlags = 0, IDirectSoundBuffer *lpBuffer = NULL)
		{
			return player.Begin(hFile, dwFlags, 0x10000, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, lpBuffer);
		}

		FakeDirectSoundBuffer &NewBuffer()
		{
			buffers.emplace_back(new FakeDirectSoundBuffer());
			return *buffers.back();
		}
	};
}

TEST_CASE_FIXTURE(PlaylistFixture, "A stream the playlist file replaces plays the playlist's track, read through Storm")
{
	HANDLE hGame = Open("MUSIC\\Title.wav");
	REQUIRE(Begin(hGame));

	std::shared_ptr<FakeSoundState> sound = engine.Last();
	CHECK(sound->Contents() == "title track");
	CHECK(sound->bStarted);
	CHECK(engine.sounds.size() == 1);
	REQUIRE(FakeStorm::OpenedHandles().size() == 1);
	HANDLE hTrack = FakeStorm::OpenedHandles()[0];

	SUBCASE("Its position and length are the track's")
	{
		sound->nCursor = 1000;
		DWORD nPos = 0, nLength = 0;
		REQUIRE(player.GetPos(hGame, &nPos, &nLength));
		CHECK(nPos == 4000);
		CHECK(nLength == 441000 * 4);
	}

	SUBCASE("When the visit ends, the game sees the stream end, and the track's file is closed")
	{
		sound->bPlaying = false;
		player.Update(TRUE);
		CHECK_FALSE(player.GetPos(hGame, NULL, NULL));
		CHECK(sound->bDestroyed);
		CHECK(FakeStorm::CloseCount(hTrack) == 1);
		CHECK(FakeStorm::CloseCount(hGame) == 0);

		// The game's own file is the game's to close
		CHECK(player.End(hGame));
		CHECK(player.CloseFile(hGame));
		CHECK(FakeStorm::CloseCount(hGame) == 1);
	}

	SUBCASE("Ending the stream closes the track's file, and closing the game's file closes only that")
	{
		CHECK(player.CloseFile(hGame));
		CHECK(FakeStorm::CloseCount(hGame) == 0);
		CHECK(player.End(hGame));
		CHECK(FakeStorm::CloseCount(hTrack) == 1);
		CHECK(FakeStorm::CloseCount(hGame) == 1);
	}

	SUBCASE("Volume and pan apply to the track")
	{
		CHECK(player.SetVolume(hGame, -2000, 1000));
		CHECK(sound->nVolume == doctest::Approx(DdaVolumeToLinear(-2000)));
		CHECK(sound->nPan == doctest::Approx(DdaPanToLinear(1000)));
	}

	SUBCASE("Losing the focus mutes it")
	{
		player.Update(FALSE);
		CHECK(sound->bMuted);
		player.Update(TRUE);
		CHECK_FALSE(sound->bMuted);
	}
}

TEST_CASE_FIXTURE(PlaylistFixture, "A whole playlist plays in one stream, and the game only sees it end at the end")
{
	HANDLE hGame = Open("music\\terran1.wav");
	REQUIRE(Begin(hGame));
	std::shared_ptr<FakeSoundState> first = engine.Last();
	CHECK(first->Contents() == "terran track 1");

	first->bPlaying = false;
	player.Update(TRUE);
	CHECK(player.GetPos(hGame, NULL, NULL));
	std::shared_ptr<FakeSoundState> second = engine.Last();
	CHECK(second->Contents() == "terran track 2");
	CHECK(second->bStarted);

	second->bPlaying = false;
	player.Update(TRUE);
	CHECK_FALSE(player.GetPos(hGame, NULL, NULL));

	// Both tracks' files were closed with their sounds
	REQUIRE(FakeStorm::OpenedHandles().size() == 2);
	for (HANDLE hTrack : FakeStorm::OpenedHandles())
		CHECK(FakeStorm::CloseCount(hTrack) == 1);
}

TEST_CASE_FIXTURE(PlaylistFixture, "A game-supplied buffer follows the playlist")
{
	FakeDirectSoundBuffer &buffer = NewBuffer();
	HANDLE hGame = Open("music\\title.wav");
	REQUIRE(Begin(hGame, 0, &buffer));
	std::shared_ptr<FakeSoundState> sound = engine.Last();
	CHECK(sound->Contents() == "title track");
	CHECK(buffer.bPlaying);

	SUBCASE("Volume set on the buffer applies to the track")
	{
		buffer.nVolume = -1500;
		player.Update(TRUE);
		CHECK(sound->nVolume == doctest::Approx(DdaVolumeToLinear(-1500)));
	}

	SUBCASE("The buffer stops when the visit ends, as Diablo II polls it")
	{
		sound->bPlaying = false;
		player.Update(TRUE);
		CHECK_FALSE(buffer.bPlaying);
	}
}

TEST_CASE_FIXTURE(PlaylistFixture, "Files whose names aren't known, or that no transition is for, play as the game asked")
{
	HANDLE hUnnamed = FakeStorm::AddFile("unnamed");
	REQUIRE(Begin(hUnnamed));
	CHECK(engine.Last()->Contents() == "unnamed");

	HANDLE hOther = Open("music\\zerg1.wav", "zerg");
	REQUIRE(Begin(hOther));
	CHECK(engine.Last()->Contents() == "zerg");

	SUBCASE("Without the names at all")
	{
		CDdaPlayer bare;
		bare.Initialize(FakeStorm::Functions(), &engine);
		bare.SetBasePlaylist(Parse(PLAYLIST));
		HANDLE hTitle = Open("music\\title.wav", "the game's title");
		REQUIRE(bare.Begin(hTitle, 0, 0x10000, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, NULL));
		CHECK(engine.Last()->Contents() == "the game's title");
		bare.StopAll();
	}
}

TEST_CASE_FIXTURE(PlaylistFixture, "A playlist none of whose tracks can be found leaves the game's file playing, and says so")
{
	player.SetBasePlaylist(Parse("version = 1\n[on]\n'music\\*' = 'gone'\n[playlist.gone]\ntracks = ['music\\nowhere.ogg']\n"));
	HANDLE hGame = Open("music\\title.wav");
	REQUIRE(Begin(hGame));
	CHECK(engine.Last()->Contents() == "the game's own music");
	CHECK(log.Warned("track 'music\\nowhere.ogg' can't be found"));
	CHECK(log.Warned("none of its tracks can be played"));
}

TEST_CASE_FIXTURE(PlaylistFixture, "A track with an absolute path, from a playlist file on disk, is read from disk")
{
	char szPath[MAX_PATH];
	REQUIRE(GetFullPathNameA("fixtures\\wav_u8_22050_mono.wav", sizeof(szPath), szPath, NULL));
	player.SetBasePlaylist(Parse("version = 1\n[on]\n'music\\*' = 'disk'\n[playlist.disk]\ntracks = ['" + std::string(szPath) + "']\n", false, true));

	HANDLE hGame = Open("music\\title.wav");
	REQUIRE(Begin(hGame));

	std::ifstream file(szPath, std::ios::binary);
	std::ostringstream contents;
	contents << file.rdbuf();
	CHECK(engine.Last()->Contents() == contents.str());
	CHECK(FakeStorm::OpenedHandles().empty());
}

TEST_CASE_FIXTURE(PlaylistFixture, "A map's playlist file is laid over the base while its archive is open")
{
	FakeStorm::AddNamedFile("music\\map.ogg", "map track");
	HANDLE hMpq = (HANDLE)0xA2C;
	CHECK(player.MapPlaylistsAllowed());
	REQUIRE(player.PushMapPlaylist(hMpq, Parse("version = 1\n[on]\n'music\\*' = 'map'\n[playlist.map]\ntracks = ['music\\map.ogg']\n", true)));

	HANDLE hGame = Open("music\\zerg1.wav");
	REQUIRE(Begin(hGame));
	std::shared_ptr<FakeSoundState> sound = engine.Last();
	CHECK(sound->Contents() == "map track");
	HANDLE hTrack = FakeStorm::OpenedHandles().back();

	// The map's tracks stop, and their files are closed, before the archive is
	CHECK(player.PopMapPlaylist(hMpq));
	CHECK(sound->bDestroyed);
	CHECK(FakeStorm::CloseCount(hTrack) == 1);
	CHECK_FALSE(player.PopMapPlaylist(hMpq));

	// music\zerg1.wav has no transition in the base, so the game's file plays from the start
	CHECK(engine.Last()->Contents() == "the game's own music");
	CHECK(player.GetPos(hGame, NULL, NULL));

	SUBCASE("Not if the base file says so")
	{
		player.SetBasePlaylist(Parse("version = 1\nmap_playlists = false\n"));
		CHECK_FALSE(player.MapPlaylistsAllowed());
		CHECK_FALSE(player.PushMapPlaylist(hMpq, Parse("version = 1\n", true)));
	}
}
