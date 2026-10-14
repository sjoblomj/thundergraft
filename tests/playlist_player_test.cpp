// Tests of CPlaylistPlayer, which plays the game's streams and the playlists that replace them, against a fake engine

#include <doctest.h>

#include "fakes/SharedPtrStrings.h"

#include "PlaylistPlayer.h"

#include <algorithm>

#include "fakes/FakeAudioEngine.h"
#include "fakes/FakePlaylistFiles.h"

namespace
{
	const std::string NAME_TITLE = "music\\title.wav";
	const std::string NAME_TERRAN1 = "music\\terran1.wav";
	const std::string NAME_TERRAN2 = "music\\terran2.wav";
	const std::string NAME_BOSS_CUE = "staredit\\wav\\boss.wav";
	const std::string NAME_REPORT = "staredit\\wav\\report.wav";

	// The playlist file from the design, with crossfades left out unless a test wants them
	const char *const EXAMPLE = R"(version = 1
[on]
'music\title.wav'    = "titlelist"
'music\terran?.wav'  = "terranlist"
'staredit\wav\boss.wav' = { music = "boss", fade_out = 3 }

[playlist.titlelist]
tracks = ['music\title.ogg']

[playlist.terranlist]
tracks = ['music\t1.ogg', 'music\t2.ogg', 'music\t3.ogg']
on.'staredit\wav\darkness.wav' = { music = "dark_terran" }

[playlist.dark_terran]
tracks = ['music\dark_t1.ogg', 'music\dark_t2.ogg']
on.'music\terran?.wav' = "dark_terran"
on.'staredit\wav\report.wav' = "report_dark"

[playlist.report_dark]
tracks = ['staredit\wav\report_dark.ogg']

[playlist.boss]
play = "all"
tracks = ['music\boss_intro.ogg', 'music\boss.ogg']
next = "boss_end"

[playlist.boss_end]
tracks = ['music\boss_end.ogg']
next = { play = "back", fade_in = 2 }
)";

	std::shared_ptr<const PlaylistFile> Parse(const std::string &text, bool bMap = false, const std::string &label = "")
	{
		PlaylistReadOptions options;
		options.label = !label.empty() ? label : bMap ? "map.toml" : "playlist.toml";
		options.bMap = bMap;
		PlaylistDiagnostics diagnostics;
		std::shared_ptr<const PlaylistFile> file = ReadPlaylistFile(text, options, diagnostics);
		INFO(diagnostics.Format());
		REQUIRE(file);
		return file;
	}

	std::string Track(const std::string &path)
	{
		return "track:" + path;
	}

	std::string Game(PlaylistStreamId id)
	{
		return "game:" + std::to_string(id);
	}

	struct Fixture
	{
		FakeAudioEngine engine;
		FakePlaylistFiles files;
		FakePlaylistLog log;
		CPlaylistPlayer player;
		uint64_t nNow = 0;

		Fixture()
		{
			player.Initialize(&engine, &files, &log, 1);
		}

		~Fixture()
		{
			player.EndAll();
		}

		void Load(const std::string &text)
		{
			player.SetBase(Parse(text));
		}

		bool Start(PlaylistStreamId id, const std::string &name, bool bLoop = false, uint64_t nStartBytes = 0)
		{
			return player.Begin(id, &name, bLoop, nStartBytes, PlaylistStreamSettings());
		}

		std::shared_ptr<FakeSoundState> Sound(PlaylistStreamId id)
		{
			FakeAudioSound *lpSound = dynamic_cast<FakeAudioSound *>(player.CurrentSound(id));
			return lpSound ? lpSound->State() : nullptr;
		}

		// What the stream plays: the contents of its current sound's file
		std::string Playing(PlaylistStreamId id)
		{
			std::shared_ptr<FakeSoundState> sound = Sound(id);
			return sound ? sound->Contents() : "";
		}

		void Update()
		{
			player.Update(nNow);
		}

		void Advance(uint64_t nMilliseconds)
		{
			nNow += nMilliseconds;
			Update();
		}

		// The current track of the stream plays to its end
		void EndTrack(PlaylistStreamId id)
		{
			std::shared_ptr<FakeSoundState> sound = Sound(id);
			REQUIRE(sound);
			sound->nCursor = sound->nLength;
			sound->bPlaying = false;
			Update();
		}

		// Moves the current track to the given number of milliseconds before its end
		void NearEnd(PlaylistStreamId id, uint64_t nMilliseconds)
		{
			std::shared_ptr<FakeSoundState> sound = Sound(id);
			REQUIRE(sound);
			sound->nCursor = sound->nLength - nMilliseconds * sound->nSampleRate / 1000;
			Update();
		}

		std::string Current()
		{
			PlaylistRef current = player.CurrentPlaylist();
			return current ? current.playlist->name : "";
		}

		PlaylistStreamId Music()
		{
			PlaylistStreamId id = 0;
			player.GetMusicStream(&id);
			return id;
		}
	};
}

TEST_CASE_FIXTURE(Fixture, "Without a playlist file, streams play the game's files")
{
	REQUIRE(Start(1, NAME_TITLE));
	CHECK(Playing(1) == Game(1));
	CHECK(Sound(1)->bStarted);
	CHECK(player.IsPlaying(1));
	CHECK(files.openedTracks.empty());
	CHECK(log.trace.empty());

	SUBCASE("They end when the file does")
	{
		Sound(1)->bPlaying = false;
		CHECK_FALSE(player.IsPlaying(1));
	}

	SUBCASE("Ending them destroys the sound")
	{
		std::shared_ptr<FakeSoundState> sound = Sound(1);
		CHECK(player.End(1));
		CHECK(sound->bDestroyed);
		CHECK_FALSE(player.HasStream(1));
		CHECK_FALSE(player.End(1));
	}

	SUBCASE("A start position is in bytes of 16-bit PCM")
	{
		REQUIRE(Start(2, NAME_TITLE, false, 4000));
		CHECK(Sound(2)->nSeekedTo == 1000);
	}

	SUBCASE("A file that can't be decoded fails")
	{
		engine.bCanDecode = false;
		CHECK_FALSE(Start(3, NAME_TITLE));
		CHECK_FALSE(player.HasStream(3));
	}
}

TEST_CASE_FIXTURE(Fixture, "Settings apply to the stream's sound")
{
	PlaylistStreamSettings settings;
	settings.nVolume = 0.5f;
	settings.nPan = -0.25f;
	settings.nPitch = 1.5f;
	settings.bMuted = true;
	REQUIRE(player.Begin(1, &NAME_TITLE, false, 0, settings));

	std::shared_ptr<FakeSoundState> sound = Sound(1);
	CHECK(sound->nVolume == 0.5f);
	CHECK(sound->nPan == -0.25f);
	CHECK(sound->nPitch == 1.5f);
	CHECK(sound->bMuted);

	player.SetVolume(1, 0.25f);
	player.SetPan(1, 1);
	player.SetPitch(1, 2);
	player.SetMuted(1, false);
	CHECK(sound->nVolume == 0.25f);
	CHECK(sound->nPan == 1);
	CHECK(sound->nPitch == 2);
	CHECK_FALSE(sound->bMuted);
}

TEST_CASE_FIXTURE(Fixture, "Music the playlists replace plays a playlist's tracks instead")
{
	Load(EXAMPLE);
	REQUIRE(Start(1, "MUSIC/Title.WAV"));

	CHECK(Playing(1) == Track("music\\title.ogg"));
	CHECK(Sound(1)->bStarted);
	CHECK(Current() == "titlelist");
	CHECK(Music() == 1);
	CHECK(log.Traced("start 'music\\title.wav' in s1 (music): [on] 'music\\title.wav' (playlist.toml:3) -> \"titlelist\""));

	SUBCASE("The game's file isn't even opened")
	{
		CHECK(files.openedGameFiles.empty());
	}

	SUBCASE("Position and length are the track's")
	{
		Sound(1)->nCursor = 1234;
		Sound(1)->nLength = 5000;
		PlaylistStreamPosition position;
		REQUIRE(player.GetPosition(1, &position));
		CHECK(position.nFrame == 1234);
		CHECK(position.bLengthKnown);
		CHECK(position.nLength == 5000);
		CHECK(position.nChannels == 2);
	}

	SUBCASE("When the visit ends, the game is told the stream ended")
	{
		EndTrack(1);
		CHECK_FALSE(player.IsPlaying(1));
		CHECK(log.Traced("the visit of \"titlelist\" ends, and the game is told the stream ended"));
		// The playlist stays current until another transition
		CHECK(Current() == "titlelist");
	}
}

TEST_CASE_FIXTURE(Fixture, "play = one plays the next track each time the game starts the music")
{
	Load(EXAMPLE);

	const char *const expected[] = { "music\\t1.ogg", "music\\t2.ogg", "music\\t3.ogg", "music\\t1.ogg" };
	PlaylistStreamId id = 1;
	for (const char *lpszTrack : expected)
	{
		CAPTURE(lpszTrack);
		REQUIRE(Start(id, id % 2 ? NAME_TERRAN1 : NAME_TERRAN2));
		CHECK(Playing(id) == Track(lpszTrack));
		EndTrack(id);
		CHECK_FALSE(player.IsPlaying(id));
		// As StarCraft does once it sees the music has ended
		player.End(id);
		id++;
	}
}

TEST_CASE_FIXTURE(Fixture, "The current playlist's transitions come first")
{
	Load(EXAMPLE);
	REQUIRE(Start(1, NAME_TERRAN1));
	CHECK(Current() == "terranlist");

	// A map's cue, only known to terranlist
	REQUIRE(Start(2, "staredit\\wav\\darkness.wav"));
	CHECK(Playing(2) == Game(2));
	CHECK(Current() == "dark_terran");
	CHECK(Playing(1) == Track("music\\dark_t1.ogg"));

	SUBCASE("The mode survives the game changing tracks")
	{
		EndTrack(1);
		player.End(1);
		REQUIRE(Start(3, NAME_TERRAN2));
		CHECK(Current() == "dark_terran");
		CHECK(Playing(3) == Track("music\\dark_t2.ogg"));
		CHECK(log.Traced("playlist \"dark_terran\": on 'music\\terran?.wav'"));
	}

	SUBCASE("A transmission is replaced while it's dark, without touching the music")
	{
		REQUIRE(Start(4, NAME_REPORT));
		CHECK(Playing(4) == Track("staredit\\wav\\report_dark.ogg"));
		CHECK(Current() == "dark_terran");
		CHECK(Music() == 1);
		CHECK(Playing(1) == Track("music\\dark_t1.ogg"));

		// When the replacement ends, so does the transmission, as far as the game knows
		EndTrack(4);
		CHECK_FALSE(player.IsPlaying(4));
		CHECK(player.IsPlaying(1));
	}

	SUBCASE("The title screen ends the mode")
	{
		player.End(1);
		REQUIRE(Start(5, NAME_TITLE));
		CHECK(Current() == "titlelist");
	}
}

TEST_CASE_FIXTURE(Fixture, "The game stopping the music while a playlist plays stops the playlist")
{
	Load(EXAMPLE);
	REQUIRE(Start(1, NAME_TERRAN1));
	REQUIRE(Start(2, "staredit\\wav\\darkness.wav"));
	CHECK(Current() == "dark_terran");

	player.End(1);
	CHECK(Current() == "");
	CHECK(Music() == 0);

	// So the game's next Terran track goes by the top-level transitions again
	REQUIRE(Start(3, NAME_TERRAN2));
	CHECK(Current() == "terranlist");
}

TEST_CASE_FIXTURE(Fixture, "Music with no transition plays the game's file, and there is no current playlist")
{
	Load(EXAMPLE);
	REQUIRE(Start(1, NAME_TERRAN1));
	REQUIRE(Start(2, "music\\zerg1.wav"));

	CHECK(Playing(2) == Game(2));
	CHECK(Music() == 2);
	CHECK(Current() == "");
	CHECK(log.Traced("no transition, so the game's file plays"));

	SUBCASE("The older music stream plays on until the game ends it")
	{
		CHECK(player.IsPlaying(1));
		CHECK(Playing(1) == Track("music\\t1.ogg"));
	}
}

TEST_CASE_FIXTURE(Fixture, "Sounds that aren't music don't change the current playlist")
{
	Load("version = 1\n[on]\n'music\\*' = 'm'\n'sound\\line.wav' = 'line'\n[playlist.m]\ntracks = ['m.ogg']\n[playlist.line]\ntracks = ['line2.ogg']\n");
	REQUIRE(Start(1, NAME_TITLE));
	REQUIRE(Start(2, "sound\\line.wav"));
	CHECK(Playing(2) == Track("line2.ogg"));
	CHECK(Current() == "m");
	CHECK(Music() == 1);

	SUBCASE("And sounds with no transition just play")
	{
		REQUIRE(Start(3, "sound\\other.wav"));
		CHECK(Playing(3) == Game(3));
		CHECK(Current() == "m");
	}
}

TEST_CASE_FIXTURE(Fixture, "Streams whose names aren't known play the game's file with no transitions")
{
	Load(EXAMPLE);
	REQUIRE(player.Begin(1, NULL, false, 0, PlaylistStreamSettings()));
	CHECK(Playing(1) == Game(1));
	CHECK(Music() == 0);
	CHECK(log.Traced("of a file whose name isn't known"));
}

TEST_CASE_FIXTURE(Fixture, "Going to the current playlist doesn't restart it")
{
	Load(EXAMPLE);
	REQUIRE(Start(1, NAME_TERRAN1));
	std::shared_ptr<FakeSoundState> sound = Sound(1);

	SUBCASE("If it's playing, it carries on, in the new stream")
	{
		REQUIRE(Start(2, NAME_TERRAN2));
		CHECK(Sound(2) == sound);
		CHECK_FALSE(sound->bDestroyed);
		CHECK(Music() == 2);
		CHECK_FALSE(player.IsPlaying(1));
		CHECK(log.Traced("\"terranlist\" is already playing, and carries on in s2"));

		// And takes the new stream's settings
		player.SetVolume(2, 0.5f);
		CHECK(sound->nVolume == 0.5f);
	}

	SUBCASE("If its visit has ended, a new visit starts")
	{
		EndTrack(1);
		REQUIRE(Start(2, NAME_TERRAN2));
		CHECK(Playing(2) == Track("music\\t2.ogg"));
	}
}

TEST_CASE_FIXTURE(Fixture, "A cue changes the music, fading out, and back returns to it")
{
	Load(EXAMPLE);
	REQUIRE(Start(1, NAME_TERRAN1));
	std::shared_ptr<FakeSoundState> terran = Sound(1);
	terran->nCursor = 50000;

	REQUIRE(Start(2, NAME_BOSS_CUE));

	SUBCASE("The cue itself plays as the map intended")
	{
		CHECK(Playing(2) == Game(2));
		CHECK(Music() == 1);
	}

	SUBCASE("The music fades out, then the boss music starts")
	{
		REQUIRE_FALSE(terran->fades.empty());
		CHECK(terran->fades.back().nFrom < 0);
		CHECK(terran->fades.back().nTo == 0);
		CHECK(terran->fades.back().nMilliseconds == 3000);

		std::shared_ptr<FakeSoundState> intro = engine.Find(Track("music\\boss_intro.ogg"));
		REQUIRE(intro);
		CHECK_FALSE(intro->bStarted);
		CHECK(Current() == "boss");
		CHECK(player.IsPlaying(1));

		// The position is the new track's, which hasn't started
		PlaylistStreamPosition position;
		REQUIRE(player.GetPosition(1, &position));
		CHECK(position.nFrame == 0);

		Advance(2900);
		CHECK_FALSE(intro->bStarted);
		CHECK_FALSE(terran->bDestroyed);

		Advance(100);
		CHECK(intro->bStarted);
		CHECK(intro->fades.empty());

		Advance(100);
		CHECK(terran->bDestroyed);
	}

	SUBCASE("The boss playlist plays all its tracks, then its next, then goes back")
	{
		Advance(3000);
		CHECK(Playing(1) == Track("music\\boss_intro.ogg"));
		EndTrack(1);
		CHECK(Playing(1) == Track("music\\boss.ogg"));
		EndTrack(1);
		CHECK(Playing(1) == Track("music\\boss_end.ogg"));
		CHECK(Current() == "boss_end");
		CHECK(player.HasBackPoint());
		EndTrack(1);

		// The Terran track, from where it was interrupted, fading in
		std::shared_ptr<FakeSoundState> resumed = Sound(1);
		CHECK(resumed->Contents() == Track("music\\t1.ogg"));
		CHECK(resumed != terran);
		CHECK(resumed->nSeekedTo == 50000);
		CHECK(resumed->bStarted);
		REQUIRE_FALSE(resumed->fades.empty());
		CHECK(resumed->fades.back().nFrom == 0);
		CHECK(resumed->fades.back().nTo == 1);
		CHECK(resumed->fades.back().nMilliseconds == 2000);
		CHECK(Current() == "terranlist");
		CHECK_FALSE(player.HasBackPoint());
		CHECK(player.IsPlaying(1));

		SUBCASE("The interrupted visit then ends as it would have")
		{
			EndTrack(1);
			// Once the Terran track that faded out at the cue is gone too
			Advance(100);
			CHECK_FALSE(player.IsPlaying(1));
		}
	}

	SUBCASE("Only one interruption is remembered")
	{
		Advance(3000);
		Sound(1)->nCursor = 777;
		REQUIRE(Start(3, NAME_BOSS_CUE));
		// boss is already playing, so this one carries on and doesn't interrupt
		CHECK(log.Traced("\"boss\" is already playing in the music, and carries on"));
		CHECK(player.HasBackPoint());
	}
}

TEST_CASE_FIXTURE(Fixture, "back can return to the game's own music")
{
	Load("version = 1\n[on]\n'cue.wav' = { music = 'interlude' }\n[playlist.interlude]\ntracks = ['i.ogg']\nnext = 'back'\n");
	REQUIRE(Start(1, NAME_TITLE));
	CHECK(Playing(1) == Game(1));
	Sound(1)->nCursor = 4242;

	REQUIRE(Start(2, "cue.wav"));
	CHECK(Playing(1) == Track("i.ogg"));
	CHECK(Current() == "interlude");

	SUBCASE("It reopens the game's file where it was")
	{
		EndTrack(1);
		std::shared_ptr<FakeSoundState> resumed = Sound(1);
		CHECK(resumed->Contents() == Game(1));
		CHECK(resumed->nSeekedTo == 4242);
		CHECK(resumed->bStarted);
		CHECK(Current() == "");
		CHECK(std::count(files.openedGameFiles.begin(), files.openedGameFiles.end(), 1) == 2);

		SUBCASE("which then ends when the file does, as without playlists")
		{
			resumed->bPlaying = false;
			CHECK_FALSE(player.IsPlaying(1));
		}
	}

	SUBCASE("Once the game ends the stream, there is nothing to go back to")
	{
		// Diablo II style: other music starts while the interlude plays, and the old stream is ended
		REQUIRE(Start(3, "music\\other.wav"));
		player.End(1);
		CHECK_FALSE(player.HasBackPoint());
	}
}

TEST_CASE_FIXTURE(Fixture, "back with nothing to go back to acts as if there were no target")
{
	Load("version = 1\n[on]\n'music\\*' = 'm'\n'cue.wav' = { music = 'back' }\n'music\\b.wav' = 'back'\n[playlist.m]\ntracks = ['m.ogg']\nnext = 'back'\n");

	REQUIRE(Start(1, "music\\a.wav"));
	CHECK(Playing(1) == Track("m.ogg"));

	SUBCASE("As next, the visit ends")
	{
		EndTrack(1);
		CHECK_FALSE(player.IsPlaying(1));
		CHECK(log.Traced("can't be followed"));
	}

	SUBCASE("As a cue, nothing happens")
	{
		REQUIRE(Start(2, "cue.wav"));
		CHECK(Playing(1) == Track("m.ogg"));
		CHECK(log.Traced("there is nothing to go back to"));
	}

	SUBCASE("On a music start, the game's file plays")
	{
		REQUIRE(Start(3, "music\\b.wav"));
		CHECK(Playing(3) == Game(3));
		CHECK(Current() == "");
	}
}

TEST_CASE_FIXTURE(Fixture, "A cue while no music plays is ignored")
{
	Load(EXAMPLE);
	REQUIRE(Start(1, NAME_BOSS_CUE));
	CHECK(Playing(1) == Game(1));
	CHECK(Current() == "");
	CHECK(log.Traced("no music is playing, so the music isn't changed"));

	SUBCASE("Also when the music has ended")
	{
		REQUIRE(Start(2, NAME_TITLE));
		EndTrack(2);
		REQUIRE(Start(3, NAME_BOSS_CUE));
		CHECK(Current() == "titlelist");
	}
}

TEST_CASE_FIXTURE(Fixture, "Crossfading cues fade both at once")
{
	Load("version = 1\n[on]\n'music\\*' = 'a'\n'cue.wav' = { music = 'b', crossfade = 1.5 }\n[playlist.a]\ntracks = ['a.ogg']\n[playlist.b]\ntracks = ['b.ogg']\n");
	REQUIRE(Start(1, NAME_TITLE));
	std::shared_ptr<FakeSoundState> a = Sound(1);
	REQUIRE(Start(2, "cue.wav"));

	std::shared_ptr<FakeSoundState> b = engine.Find(Track("b.ogg"));
	REQUIRE(b);
	CHECK(b->bStarted);
	REQUIRE(b->fades.size() == 1);
	CHECK(b->fades[0].nFrom == 0);
	CHECK(b->fades[0].nTo == 1);
	CHECK(b->fades[0].nMilliseconds == 1500);
	REQUIRE(a->fades.size() == 1);
	CHECK(a->fades[0].nTo == 0);
	CHECK(a->fades[0].nMilliseconds == 1500);
	CHECK(Playing(1) == Track("b.ogg"));

	Advance(1600);
	CHECK(a->bDestroyed);
	CHECK_FALSE(b->bDestroyed);
}

TEST_CASE_FIXTURE(Fixture, "A new music stream can fade out the old one")
{
	Load("version = 1\n[on]\n'music\\a.wav' = 'a'\n'music\\b.wav' = { play = 'b', fade_out = 2, fade_in = 1 }\n'music\\c.wav' = { play = 'b', crossfade = 2 }\n'music\\d.wav' = 'b'\n[playlist.a]\ntracks = ['a.ogg']\n[playlist.b]\ntracks = ['b.ogg']\n");
	REQUIRE(Start(1, "music\\a.wav"));
	std::shared_ptr<FakeSoundState> a = Sound(1);

	SUBCASE("fade_out: the new one waits")
	{
		REQUIRE(Start(2, "music\\b.wav"));
		CHECK(a->fades.back().nTo == 0);
		std::shared_ptr<FakeSoundState> b = Sound(2);
		CHECK_FALSE(b->bStarted);
		CHECK(player.IsPlaying(1));

		Advance(2000);
		CHECK(b->bStarted);
		REQUIRE(b->fades.size() == 1);
		CHECK(b->fades[0].nMilliseconds == 1000);

		// The old stream ends once its fade is over
		Advance(100);
		CHECK(a->bDestroyed);
		CHECK_FALSE(player.IsPlaying(1));
	}

	SUBCASE("crossfade: both at once")
	{
		REQUIRE(Start(2, "music\\c.wav"));
		std::shared_ptr<FakeSoundState> b = Sound(2);
		CHECK(b->bStarted);
		CHECK(b->fades[0].nMilliseconds == 2000);
		CHECK(a->fades.back().nMilliseconds == 2000);
	}

	SUBCASE("No fades: the old one plays on until the game ends it")
	{
		REQUIRE(Start(2, "music\\d.wav"));
		CHECK(a->fades.empty());
		Advance(10000);
		CHECK(player.IsPlaying(1));
		CHECK_FALSE(a->bDestroyed);
	}
}

TEST_CASE_FIXTURE(Fixture, "stop silences a stream")
{
	Load("version = 1\n[on]\n'music\\*' = 'm'\n'sound\\x.wav' = 'stop'\n'music\\quiet.wav' = 'stop'\n'cue.wav' = { music = 'stop', fade_out = 1 }\n[playlist.m]\ntracks = ['m.ogg']\n");

	SUBCASE("A sound that isn't music ends at once")
	{
		REQUIRE(Start(1, "sound\\x.wav"));
		CHECK_FALSE(player.IsPlaying(1));
		CHECK(files.openedGameFiles.empty());
	}

	SUBCASE("Music ends, and there is no current playlist")
	{
		REQUIRE(Start(1, NAME_TITLE));
		REQUIRE(Start(2, "music\\quiet.wav"));
		CHECK_FALSE(player.IsPlaying(2));
		CHECK(Current() == "");
		CHECK(Music() == 2);
	}

	SUBCASE("A cue can fade the music out, and the game is told once it has")
	{
		REQUIRE(Start(1, NAME_TITLE));
		std::shared_ptr<FakeSoundState> m = Sound(1);
		REQUIRE(Start(2, "cue.wav"));
		CHECK(m->fades.back().nTo == 0);
		CHECK(player.IsPlaying(1));
		CHECK(Current() == "");
		Advance(1100);
		CHECK_FALSE(player.IsPlaying(1));
		CHECK(m->bDestroyed);
		// And back can return to it
		CHECK(player.HasBackPoint());
	}
}

TEST_CASE_FIXTURE(Fixture, "A playlist replacing a sound can change the music when it ends")
{
	Load("version = 1\n[on]\n'music\\*' = 'm'\n'sound\\line.wav' = 'line'\n[playlist.m]\ntracks = ['m.ogg']\n[playlist.boss]\ntracks = ['boss.ogg']\n[playlist.line]\ntracks = ['line.ogg']\nnext = { music = 'boss', crossfade = 1 }\n");
	REQUIRE(Start(1, NAME_TITLE));
	REQUIRE(Start(2, "sound\\line.wav"));
	CHECK(Playing(2) == Track("line.ogg"));

	EndTrack(2);
	CHECK_FALSE(player.IsPlaying(2));
	CHECK(Playing(1) == Track("boss.ogg"));
	CHECK(Current() == "boss");
	// A next doesn't interrupt anything
	CHECK_FALSE(player.HasBackPoint());
}

TEST_CASE_FIXTURE(Fixture, "next chains playlists in the same stream")
{
	Load("version = 1\n[on]\n'music\\*' = 'a'\n[playlist.a]\ntracks = ['a.ogg']\nnext = 'b'\n[playlist.b]\ntracks = ['b1.ogg', 'b2.ogg']\nnext = 'b'\n");
	REQUIRE(Start(1, NAME_TITLE));
	EndTrack(1);
	CHECK(Playing(1) == Track("b1.ogg"));
	CHECK(Current() == "b");
	EndTrack(1);
	CHECK(Playing(1) == Track("b2.ogg"));
	EndTrack(1);
	// Going to itself after its visit has ended starts a new visit
	CHECK(Playing(1) == Track("b1.ogg"));
	CHECK(player.IsPlaying(1));
}

TEST_CASE_FIXTURE(Fixture, "The next track is opened before the current one ends, to start without a gap")
{
	Load("version = 1\n[on]\n'music\\*' = 'a'\n[playlist.a]\nplay = 'all'\ntracks = ['a1.ogg', 'a2.ogg']\n");
	REQUIRE(Start(1, NAME_TITLE));
	std::shared_ptr<FakeSoundState> a1 = Sound(1);

	NearEnd(1, 5000);
	CHECK_FALSE(engine.Find(Track("a2.ogg")));

	NearEnd(1, 2000);
	std::shared_ptr<FakeSoundState> a2 = engine.Find(Track("a2.ogg"));
	REQUIRE(a2);
	CHECK(a2->bStarted);
	CHECK(a2->lpStartsAfter == a1);
	CHECK(Playing(1) == Track("a1.ogg"));

	EndTrack(1);
	CHECK(Sound(1) == a2);
	CHECK(a1->bDestroyed);
	CHECK(engine.CountOpened(Track("a2.ogg")) == 1);

	SUBCASE("If the engine can't schedule it, it starts when the other ends")
	{
		engine.bCanStartAfter = false;
		Load("version = 1\n[on]\n'music\\*' = 'a'\n[playlist.a]\nplay = 'all'\ntracks = ['a1.ogg', 'a2.ogg']\n");
		REQUIRE(Start(2, NAME_TITLE));
		NearEnd(2, 1000);
		std::shared_ptr<FakeSoundState> second = engine.Find(Track("a2.ogg"));
		REQUIRE(second);
		CHECK_FALSE(second->bStarted);
		EndTrack(2);
		CHECK(second->bStarted);
		CHECK(Sound(2) == second);
	}

	SUBCASE("Without a known length, the next track is opened when the current one ends")
	{
		// Otherwise the new stream would carry on with the playlist that is playing
		player.End(1);
		REQUIRE(Start(3, NAME_TITLE));
		Sound(3)->bLengthKnown = false;
		Advance(100);
		size_t nOpened = engine.sounds.size();
		EndTrack(3);
		CHECK(engine.sounds.size() == nOpened + 1);
		CHECK(Playing(3) == Track("a2.ogg"));
	}

	SUBCASE("A cue before the end throws the prepared track away")
	{
		player.End(1);
		REQUIRE(Start(4, NAME_TITLE));
		NearEnd(4, 1000);
		std::shared_ptr<FakeSoundState> prepared = engine.Last();
		CHECK(prepared->Contents() == Track("a2.ogg"));
		player.End(4);
		CHECK(prepared->bDestroyed);
	}
}

TEST_CASE_FIXTURE(Fixture, "Tracks of a playlist with crossfade overlap")
{
	Load("version = 1\n[on]\n'music\\*' = 'a'\n[playlist.a]\nplay = 'all'\ncrossfade = 3\ntracks = ['a1.ogg', 'a2.ogg']\n");
	REQUIRE(Start(1, NAME_TITLE));
	std::shared_ptr<FakeSoundState> a1 = Sound(1);

	NearEnd(1, 3400);
	std::shared_ptr<FakeSoundState> a2 = engine.Find(Track("a2.ogg"));
	REQUIRE(a2);
	CHECK_FALSE(a2->bStarted);
	CHECK(a2->lpStartsAfter == nullptr);

	NearEnd(1, 3000);
	CHECK(a2->bStarted);
	REQUIRE(a2->fades.size() == 1);
	CHECK(a2->fades[0].nFrom == 0);
	CHECK(a2->fades[0].nMilliseconds == 3000);
	REQUIRE(a1->fades.size() == 1);
	CHECK(a1->fades[0].nTo == 0);
	CHECK(Playing(1) == Track("a2.ogg"));

	// The old track's end doesn't end the stream
	a1->bPlaying = false;
	Advance(10);
	CHECK(player.IsPlaying(1));
	CHECK(a1->bDestroyed);
}

TEST_CASE_FIXTURE(Fixture, "fade_out in next fades out the end of the last track")
{
	Load("version = 1\n[on]\n'music\\*' = 'a'\n[playlist.a]\ntracks = ['a.ogg']\nnext = { play = 'b', fade_out = 1 }\n[playlist.b]\ntracks = ['b.ogg']\n");
	REQUIRE(Start(1, NAME_TITLE));
	std::shared_ptr<FakeSoundState> a = Sound(1);

	NearEnd(1, 2000);
	CHECK(a->fades.empty());
	NearEnd(1, 900);
	REQUIRE(a->fades.size() == 1);
	CHECK(a->fades[0].nTo == 0);
	CHECK(a->fades[0].nMilliseconds == 900);

	EndTrack(1);
	CHECK(Playing(1) == Track("b.ogg"));
}

TEST_CASE_FIXTURE(Fixture, "A looping stream loops a play = one playlist's track")
{
	Load(EXAMPLE);
	REQUIRE(Start(1, NAME_TITLE, true));
	CHECK(Sound(1)->bLoop);
	NearEnd(1, 100);
	Advance(5000);
	CHECK(engine.sounds.size() == 1);
	CHECK(player.IsPlaying(1));
}

TEST_CASE_FIXTURE(Fixture, "Missing tracks are skipped, with a warning")
{
	Load(EXAMPLE);
	files.missing.insert("music\\t1.ogg");

	REQUIRE(Start(1, NAME_TERRAN1));
	CHECK(Playing(1) == Track("music\\t2.ogg"));
	REQUIRE(log.warnings.size() == 1);
	CHECK(log.warnings[0].file == "playlist.toml");
	CHECK(log.warnings[0].nLine == 11);
	CHECK(log.Warned("track 'music\\t1.ogg' can't be found, so it is skipped"));

	SUBCASE("Each is only warned about once")
	{
		EndTrack(1);
		player.End(1);
		REQUIRE(Start(2, NAME_TERRAN1));
		EndTrack(2);
		player.End(2);
		REQUIRE(Start(3, NAME_TERRAN1));
		CHECK(Playing(3) == Track("music\\t2.ogg"));
		CHECK(log.warnings.size() == 1);
	}

	SUBCASE("Tracks that can't be decoded too")
	{
		engine.undecodable.push_back(Track("music\\t3.ogg"));
		EndTrack(1);
		player.End(1);
		REQUIRE(Start(2, NAME_TERRAN1));
		CHECK(Playing(2) == Track("music\\t2.ogg"));
		CHECK(log.Warned("isn't in a format ThunderGraft can play"));
	}
}

TEST_CASE_FIXTURE(Fixture, "A playlist none of whose tracks play leaves the game's file playing")
{
	Load(EXAMPLE);
	files.missing.insert("music\\title.ogg");
	REQUIRE(Start(1, NAME_TITLE));
	CHECK(Playing(1) == Game(1));
	CHECK(Current() == "");
	CHECK(log.Warned("none of its tracks can be played"));

	SUBCASE("A cue to it leaves the music alone")
	{
		Load("version = 1\n[on]\n'music\\*' = 'm'\n'cue.wav' = { music = 'gone' }\n[playlist.m]\ntracks = ['m.ogg']\n[playlist.gone]\ntracks = ['x.ogg', 'y.ogg']\n");
		files.missing.insert("x.ogg");
		files.missing.insert("y.ogg");
		REQUIRE(Start(2, NAME_TITLE));
		REQUIRE(Start(3, "cue.wav"));
		CHECK(Playing(2) == Track("m.ogg"));
		CHECK(Current() == "m");
	}

	SUBCASE("A forever playlist with nothing to play doesn't loop for ever")
	{
		Load("version = 1\n[on]\n'music\\*' = 'm'\n[playlist.m]\nplay = 'all'\ntimes = 'forever'\ntracks = ['x.ogg']\n");
		files.missing.insert("x.ogg");
		REQUIRE(Start(4, NAME_TITLE));
		CHECK(Playing(4) == Game(4));
	}
}

TEST_CASE_FIXTURE(Fixture, "A pattern without wildcards beats one with them, and ties are warned about once")
{
	Load("version = 1\n[on]\n'music\\*' = 'wild'\n'music\\title.wav' = 'exact'\n'*\\t?.wav' = 'a'\n'*\\?1.wav' = 'b'\n"
		"[playlist.wild]\ntracks = ['w.ogg']\n[playlist.exact]\ntracks = ['e.ogg']\n[playlist.a]\ntracks = ['a.ogg']\n[playlist.b]\ntracks = ['b.ogg']\n");

	REQUIRE(Start(1, NAME_TITLE));
	CHECK(Current() == "exact");
	player.End(1);

	REQUIRE(Start(2, "music\\zerg.wav"));
	CHECK(Current() == "wild");
	player.End(2);

	// *\t?.wav and *\?1.wav both have 6 characters besides the wildcards; *\?1.wav comes first
	REQUIRE(Start(3, "x\\t1.wav"));
	CHECK(Playing(3) == Track("b.ogg"));
	REQUIRE(log.warnings.size() == 1);
	CHECK(log.Warned("are equally specific"));
	player.End(3);

	REQUIRE(Start(4, "y\\t1.wav"));
	CHECK(log.warnings.size() == 1);
}

TEST_CASE_FIXTURE(Fixture, "Volume and muting apply to every track the stream plays")
{
	Load("version = 1\n[on]\n'music\\*' = 'a'\n[playlist.a]\nplay = 'all'\ntracks = ['a1.ogg', 'a2.ogg']\n");
	REQUIRE(Start(1, NAME_TITLE));
	player.SetVolume(1, 0.3f);
	player.SetMuted(1, true);

	NearEnd(1, 1000);
	std::shared_ptr<FakeSoundState> a2 = engine.Find(Track("a2.ogg"));
	REQUIRE(a2);
	CHECK(a2->nVolume == 0.3f);
	CHECK(a2->bMuted);

	player.SetMuted(1, false);
	CHECK_FALSE(a2->bMuted);
	CHECK_FALSE(Sound(1)->bMuted);
}

TEST_CASE_FIXTURE(Fixture, "A map's playlist file is laid over the base")
{
	Load(EXAMPLE);
	std::shared_ptr<const PlaylistFile> map = Parse("version = 1\n[on]\n'music\\terran*' = 'mapmusic'\n'staredit\\wav\\x.wav' = { music = 'boss' }\n"
		"[playlist.mapmusic]\ntracks = ['music\\map1.ogg', 'music\\map2.ogg']\n[playlist.titlelist]\ntracks = ['music\\maptitle.ogg']\n", true);

	// Play a Terran track before the map, so the base's pass has moved on
	REQUIRE(Start(1, NAME_TERRAN1));
	CHECK(Playing(1) == Track("music\\t1.ogg"));
	EndTrack(1);
	player.End(1);

	REQUIRE(player.PushMap(0xA1, map));
	CHECK(player.HasMap(0xA1));

	SUBCASE("Its top-level transitions come before the base's, even less specific ones")
	{
		REQUIRE(Start(2, NAME_TERRAN2));
		CHECK(Current() == "mapmusic");
		CHECK(Playing(2) == Track("music\\map1.ogg"));
	}

	SUBCASE("Its playlists hide the base's of the same name, for the base's transitions too")
	{
		REQUIRE(Start(2, NAME_TITLE));
		CHECK(Playing(2) == Track("music\\maptitle.ogg"));
	}

	SUBCASE("It can use the base's playlists")
	{
		REQUIRE(Start(2, NAME_TERRAN2));
		REQUIRE(Start(3, "staredit\\wav\\x.wav"));
		CHECK(Current() == "boss");
	}

	SUBCASE("Unloading it stops what it played, and the music starts again from the base")
	{
		REQUIRE(Start(2, NAME_TERRAN2));
		std::shared_ptr<FakeSoundState> mapTrack = Sound(2);
		CHECK(player.PopMap(0xA1));
		CHECK(mapTrack->bDestroyed);
		CHECK(Current() == "terranlist");
		// The base's pass continues where it was before the map
		CHECK(Playing(2) == Track("music\\t2.ogg"));
		CHECK_FALSE(player.PopMap(0xA1));
	}

	SUBCASE("Base tracks opened while it was loaded stop too, as Storm may have found them in the map")
	{
		REQUIRE(Start(2, "music\\zerg.wav"));
		REQUIRE(Start(3, NAME_BOSS_CUE));
		// The map's [on] has no rule for the boss cue, so the base's applies
		Advance(3000);
		std::shared_ptr<FakeSoundState> intro = Sound(2);
		CHECK(intro->Contents() == Track("music\\boss_intro.ogg"));
		player.PopMap(0xA1);
		CHECK(intro->bDestroyed);
		// music\zerg.wav has no transition in the base, so the game's file plays
		CHECK(Playing(2) == Game(2));
		CHECK(Current() == "");
	}

	SUBCASE("Sounds that aren't music and played the map's tracks end")
	{
		std::shared_ptr<const PlaylistFile> map2 = Parse("version = 1\n[on]\n'sound\\a.wav' = 'line'\n[playlist.line]\ntracks = ['l.ogg']\n", true);
		REQUIRE(player.PushMap(0xA2, map2));
		REQUIRE(Start(4, "sound\\a.wav"));
		CHECK(player.IsPlaying(4));
		player.PopMap(0xA2);
		CHECK_FALSE(player.IsPlaying(4));
	}

	SUBCASE("The game's own streams are left alone")
	{
		REQUIRE(Start(5, "sound\\other.wav"));
		std::shared_ptr<FakeSoundState> sound = Sound(5);
		player.PopMap(0xA1);
		CHECK_FALSE(sound->bDestroyed);
	}
}

TEST_CASE_FIXTURE(Fixture, "The most recently loaded map is the one in use")
{
	Load(EXAMPLE);
	REQUIRE(player.PushMap(1, Parse("version = 1\n[on]\n'music\\*' = 'one'\n[playlist.one]\ntracks = ['1.ogg']\n", true)));
	REQUIRE(player.PushMap(2, Parse("version = 1\n[on]\n'music\\*' = 'two'\n[playlist.two]\ntracks = ['2.ogg']\n", true)));

	REQUIRE(Start(1, NAME_TITLE));
	CHECK(Current() == "two");
	player.End(1);

	SUBCASE("Removing an older one leaves the newer in use")
	{
		player.PopMap(1);
		REQUIRE(Start(2, NAME_TITLE));
		CHECK(Current() == "two");
	}

	SUBCASE("Removing the newer brings back the older")
	{
		player.PopMap(2);
		REQUIRE(Start(2, NAME_TITLE));
		CHECK(Current() == "one");
	}

	SUBCASE("A tag can only be used once")
	{
		CHECK_FALSE(player.PushMap(1, Parse("version = 1\n", true)));
	}
}

TEST_CASE_FIXTURE(Fixture, "A map that doesn't inherit hides the base completely")
{
	Load(EXAMPLE);
	REQUIRE(player.PushMap(1, Parse("version = 1\ninherit = false\nmusic = ['sound\\*']\n[on]\n'sound\\x.wav' = 'mine'\n'music\\title.wav' = 'titlelist'\n[playlist.mine]\ntracks = ['mine.ogg']\n", true)));

	REQUIRE(Start(1, NAME_TERRAN1));
	CHECK(Playing(1) == Game(1));
	CHECK(Music() == 0);

	REQUIRE(Start(2, "sound\\x.wav"));
	CHECK(Current() == "mine");
	CHECK(Music() == 2);

	REQUIRE(Start(3, NAME_TITLE));
	CHECK(Playing(3) == Game(3));
	CHECK(log.Warned("no playlist named \"titlelist\" in this file;"));
}

TEST_CASE_FIXTURE(Fixture, "Names a map uses that neither file defines are warned about and ignored")
{
	Load(EXAMPLE);
	REQUIRE(player.PushMap(1, Parse("version = 1\n[on]\n'music\\title.wav' = 'nowhere'\n", true)));
	CHECK(log.Warned("no playlist named \"nowhere\" in this file or in the base file"));

	REQUIRE(Start(1, NAME_TITLE));
	CHECK(Playing(1) == Game(1));
	CHECK(log.Traced("there is no playlist named \"nowhere\", so the transition is ignored"));
}

TEST_CASE_FIXTURE(Fixture, "The base file can turn maps' playlist files off")
{
	Load("version = 1\nmap_playlists = false\n");
	CHECK_FALSE(player.MapPlaylistsAllowed());
	CHECK_FALSE(player.PushMap(1, Parse("version = 1\n", true)));
}

TEST_CASE_FIXTURE(Fixture, "A map's file works without a base file")
{
	CHECK_FALSE(player.HasPlaylists());
	REQUIRE(player.PushMap(1, Parse("version = 1\n[on]\n'music\\*' = 'm'\n[playlist.m]\ntracks = ['m.ogg']\n", true)));
	CHECK(player.HasPlaylists());
	REQUIRE(Start(1, NAME_TITLE));
	CHECK(Playing(1) == Track("m.ogg"));
}

TEST_CASE_FIXTURE(Fixture, "The music key decides which streams are music")
{
	Load("version = 1\nmusic = ['sound\\music\\*']\n[on]\n'sound\\music\\*' = 'm'\n'music\\*' = 'm'\n[playlist.m]\ntracks = ['m.ogg']\n");
	REQUIRE(Start(1, "music\\title.wav"));
	CHECK(Current() == "");
	CHECK(Music() == 0);

	REQUIRE(Start(2, "sound\\music\\x.wav"));
	CHECK(Current() == "m");
	CHECK(Music() == 2);
}

TEST_CASE_FIXTURE(Fixture, "Ending everything forgets the music")
{
	Load(EXAMPLE);
	REQUIRE(Start(1, NAME_TERRAN1));
	REQUIRE(Start(2, NAME_REPORT));
	std::shared_ptr<FakeSoundState> sound = Sound(1);
	player.EndAll();
	CHECK(sound->bDestroyed);
	CHECK(Current() == "");
	CHECK(Music() == 0);
}

TEST_CASE_FIXTURE(Fixture, "Abandoning leaves the sounds alone")
{
	Load(EXAMPLE);
	REQUIRE(Start(1, NAME_TERRAN1));
	std::shared_ptr<FakeSoundState> state = Sound(1);
	IAudioSound *lpSound = player.CurrentSound(1);
	player.Abandon();
	CHECK_FALSE(state->bDestroyed);
	CHECK_FALSE(player.HasStream(1));
	delete lpSound;
}

TEST_CASE_FIXTURE(Fixture, "Starting a stream again replaces it")
{
	Load(EXAMPLE);
	REQUIRE(Start(1, NAME_TERRAN1));
	std::shared_ptr<FakeSoundState> first = Sound(1);
	REQUIRE(Start(1, NAME_TITLE));
	CHECK(first->bDestroyed);
	CHECK(Playing(1) == Track("music\\title.ogg"));
}

TEST_CASE_FIXTURE(Fixture, "Without tracing, nothing is traced")
{
	log.bTrace = false;
	Load(EXAMPLE);
	REQUIRE(Start(1, NAME_TERRAN1));
	EndTrack(1);
	CHECK(log.trace.empty());
}
