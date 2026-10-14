// Tests of the playlist file reader

#include <doctest.h>

#include "fakes/SharedPtrStrings.h"

#include "PlaylistFile.h"

#include <string>

namespace
{
	struct Result
	{
		std::shared_ptr<const PlaylistFile> file;
		PlaylistDiagnostics diagnostics;

		// Whether some diagnostic of the severity contains the text, on the line if one is given
		bool Has(PlaylistSeverity severity, const std::string &text, unsigned nLine = 0) const
		{
			for (const PlaylistDiagnostic &diagnostic : diagnostics.All())
			{
				if (diagnostic.severity == severity && diagnostic.message.find(text) != std::string::npos && (!nLine || diagnostic.nLine == nLine))
					return true;
			}
			return false;
		}

		bool HasError(const std::string &text, unsigned nLine = 0) const
		{ return Has(PLAYLIST_ERROR, text, nLine); }
	};

	Result Read(const std::string &text, bool bMap = false, bool bFromDisk = false)
	{
		Result result;
		PlaylistReadOptions options;
		options.label = bMap ? "map.toml" : "playlist.toml";
		options.bMap = bMap;
		options.bFromDisk = bFromDisk;
		result.file = ReadPlaylistFile(text, options, result.diagnostics);
		INFO(result.diagnostics.Format());
		return result;
	}

	// A file that reads, with one playlist and the given extra lines
	std::string Minimal(const std::string &extra = "")
	{
		return "version = 1\n[playlist.p]\ntracks = ['music\\t1.ogg']\n" + extra;
	}
}

TEST_CASE("The example from the design reads")
{
	const char *lpszExample = R"(version = 1

# Transitions that apply whatever is playing
[on]
'music\title.wav'    = "titlelist"
'music\terran?.wav'  = "terranlist"
'staredit\wav\boss.wav' = { music = "boss", fade_out = 3 }

[playlist.titlelist]
tracks = ['music\title.ogg']

[playlist.terranlist]
order = "shuffle"
crossfade = 1.5
tracks = [
    'music\t1.ogg',
    { file = 'music\t2.ogg', times = 2 },
    'music\t3.ogg',
]
on.'staredit\wav\darkness.wav' = { music = "dark_terran", crossfade = 3 }

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

	Result result = Read(lpszExample);
	REQUIRE(result.file);
	CHECK(result.diagnostics.All().empty());

	const PlaylistFile &file = *result.file;
	CHECK_FALSE(file.bHasMusic);
	CHECK(file.bMapPlaylists);
	CHECK(file.playlists.size() == 6);
	REQUIRE(file.on.size() == 3);

	SUBCASE("Top-level transitions")
	{
		// toml++ keeps tables sorted by key
		const PlaylistTransition &title = file.on[1];
		CHECK(title.pattern == "music\\title.wav");
		CHECK(title.written == "music\\title.wav");
		CHECK(title.nLine == 5);
		CHECK(title.target.kind == PLAYLIST_TARGET_PLAYLIST);
		CHECK(title.target.playlist == "titlelist");
		CHECK_FALSE(title.target.bMusic);

		const PlaylistTransition &boss = file.on[2];
		CHECK(boss.pattern == "staredit\\wav\\boss.wav");
		CHECK(boss.target.bMusic);
		CHECK(boss.target.playlist == "boss");
		CHECK(boss.target.nFadeOut == 3);
		CHECK(boss.target.nFadeIn == 0);
		CHECK(boss.target.Describe() == "{ music = \"boss\", fade_out = 3 }");
	}

	SUBCASE("A shuffled playlist with a repeated track and a transition of its own")
	{
		const Playlist *lpTerran = file.Find("terranlist");
		REQUIRE(lpTerran);
		CHECK(lpTerran->bShuffle);
		CHECK_FALSE(lpTerran->bPlayAll);
		CHECK(lpTerran->nCrossfade == 1.5);
		REQUIRE(lpTerran->tracks.size() == 3);
		CHECK(lpTerran->tracks[0].file == "music\\t1.ogg");
		CHECK(lpTerran->tracks[0].nTimes == 1);
		CHECK(lpTerran->tracks[1].file == "music\\t2.ogg");
		CHECK(lpTerran->tracks[1].nTimes == 2);
		CHECK(lpTerran->tracks[1].nLine == 17);
		REQUIRE(lpTerran->on.size() == 1);
		CHECK(lpTerran->on[0].target.playlist == "dark_terran");
		CHECK(lpTerran->on[0].target.nCrossfade == 3);
	}

	SUBCASE("play = all and next")
	{
		const Playlist *lpBoss = file.Find("boss");
		REQUIRE(lpBoss);
		CHECK(lpBoss->bPlayAll);
		CHECK(lpBoss->nTimes == 1);
		CHECK(lpBoss->bHasNext);
		CHECK(lpBoss->next.playlist == "boss_end");

		const Playlist *lpEnd = file.Find("boss_end");
		REQUIRE(lpEnd);
		CHECK(lpEnd->next.kind == PLAYLIST_TARGET_BACK);
		CHECK(lpEnd->next.nFadeIn == 2);
		CHECK(lpEnd->next.Describe() == "{ play = \"back\", fade_in = 2 }");
	}
}

TEST_CASE("version = 1 is required")
{
	CHECK(Read("version = 1\n").file);
	CHECK(Read("version = 1\n").diagnostics.All().empty());
	CHECK(Read("").HasError("version = 1 is missing"));
	CHECK(Read("version = 2\n").HasError("version 2 isn't supported", 1));
	CHECK(Read("version = '1'\n").HasError("version should be 1, not a string"));
	CHECK(Read("version = 1.0\n").HasError("version should be 1, not a number"));
}

TEST_CASE("TOML syntax errors are reported with their lines")
{
	Result result = Read("version = 1\n[on\n");
	CHECK_FALSE(result.file);
	CHECK(result.diagnostics.ErrorCount() == 1);
	CHECK(result.diagnostics.All()[0].nLine == 2);

	SUBCASE("Duplicate keys are errors")
	{
		CHECK_FALSE(Read("version = 1\nversion = 1\n").file);
		CHECK_FALSE(Read(Minimal("[on]\n'a' = 'p'\n'a' = 'p'\n")).file);
	}
}

TEST_CASE("Paths in double quotes with backslashes get a hint")
{
	SUBCASE("An escape TOML doesn't know is a syntax error")
	{
		Result result = Read("version = 1\n[playlist.p]\ntracks = [\"music\\zerg1.wav\"]\n");
		CHECK_FALSE(result.file);
		CHECK(result.HasError("single quotes", 3));
	}

	SUBCASE("One it knows becomes a control character")
	{
		Result result = Read("version = 1\n[playlist.p]\ntracks = [\"music\\terran1.wav\"]\n");
		CHECK_FALSE(result.file);
		CHECK(result.HasError("control character", 3));
		CHECK(result.HasError("single quotes", 3));
	}

	SUBCASE("Slashes work in either kind of quotes")
	{
		Result result = Read("version = 1\n[playlist.p]\ntracks = [\"music/terran1.wav\", 'music/zerg1.wav']\n");
		REQUIRE(result.file);
		CHECK(result.file->Find("p")->tracks[0].file == "music\\terran1.wav");
		CHECK(result.file->Find("p")->tracks[1].file == "music\\zerg1.wav");
	}
}

TEST_CASE("Unknown keys are errors")
{
	CHECK(Read(Minimal("tracsk = []\n")).HasError("unknown key \"tracsk\"", 4));
	CHECK(Read("version = 1\nloop = true\n").HasError("unknown key \"loop\"", 2));
	CHECK(Read(Minimal("next = { play = 'p', fade = 1 }\n")).HasError("unknown key \"fade\""));
	CHECK(Read("version = 1\n[playlist.p]\ntracks = [{ file = 'a', time = 2 }]\n").HasError("unknown key \"time\""));
}

TEST_CASE("Values of the wrong type are errors")
{
	CHECK(Read(Minimal("order = 'random'\n")).HasError("order should be \"in order\" or \"shuffle\""));
	CHECK(Read(Minimal("play = 1\n")).HasError("play should be \"one\" or \"all\""));
	CHECK(Read(Minimal("crossfade = '1'\n")).HasError("crossfade should be a number of seconds"));
	CHECK(Read(Minimal("crossfade = -1\n")).HasError("from 0 to"));
	CHECK(Read(Minimal("crossfade = inf\n")).HasError("from 0 to"));
	CHECK(Read(Minimal("crossfade = nan\n")).HasError("from 0 to"));
	CHECK(Read(Minimal("crossfade = 3601\n")).HasError("from 0 to"));
	CHECK(Read("version = 1\nmusic = 'music\\*'\n").HasError("music should be a list"));
	CHECK(Read("version = 1\nmusic = [1]\n").HasError("music should be a list of file patterns"));
	CHECK(Read("version = 1\non = 1\n").HasError("[on] should be a table"));
	CHECK(Read("version = 1\nplaylist = 1\n").HasError("playlist should be a table"));
	CHECK(Read("version = 1\nplaylist.p = 1\n").HasError("playlist \"p\" should be a table"));
	CHECK(Read("version = 1\n[playlist.p]\ntracks = 'a'\n").HasError("tracks should be a list"));
	CHECK(Read("version = 1\n[playlist.p]\ntracks = [1]\n").HasError("track 1 should be a path"));
	CHECK(Read("version = 1\nmap_playlists = 'no'\n").HasError("map_playlists should be true or false"));

	SUBCASE("Durations may be whole numbers or not")
	{
		Result result = Read(Minimal("crossfade = 2\n"));
		REQUIRE(result.file);
		CHECK(result.file->Find("p")->nCrossfade == 2);
	}
}

TEST_CASE("Playlists need tracks")
{
	CHECK(Read("version = 1\n[playlist.p]\n").HasError("playlist \"p\" has no tracks", 2));
	CHECK(Read("version = 1\n[playlist.p]\ntracks = []\n").HasError("playlist \"p\" has no tracks"));
	CHECK(Read("version = 1\n[playlist.p]\ntracks = [{ times = 2 }]\n").HasError("needs a file"));
	CHECK(Read("version = 1\n[playlist.p]\ntracks = ['']\n").HasError("is empty"));
}

TEST_CASE("times")
{
	SUBCASE("On a track, it is how many times it plays in a row")
	{
		Result result = Read("version = 1\n[playlist.p]\ntracks = [{ file = 'a', times = 3 }]\n");
		REQUIRE(result.file);
		CHECK(result.file->Find("p")->tracks[0].nTimes == 3);

		CHECK(Read("version = 1\n[playlist.p]\ntracks = [{ file = 'a', times = 0 }]\n").HasError("times should be from 1"));
		CHECK(Read("version = 1\n[playlist.p]\ntracks = [{ file = 'a', times = 1.5 }]\n").HasError("times should be a whole number"));
	}

	SUBCASE("On a playlist, it needs play = all")
	{
		Result result = Read(Minimal("play = 'all'\ntimes = 3\n"));
		REQUIRE(result.file);
		CHECK(result.file->Find("p")->nTimes == 3);
		CHECK_FALSE(result.file->Find("p")->bForever);

		Result forever = Read(Minimal("play = 'all'\ntimes = 'forever'\n"));
		REQUIRE(forever.file);
		CHECK(forever.file->Find("p")->bForever);

		CHECK(Read(Minimal("times = 3\n")).HasError("only has an effect with play = \"all\""));
		CHECK(Read(Minimal("play = 'all'\ntimes = 0\n")).HasError("times should be from 1"));
		CHECK(Read(Minimal("play = 'all'\ntimes = 'always'\n")).HasError("whole number or \"forever\""));
	}
}

TEST_CASE("Targets")
{
	SUBCASE("back and stop")
	{
		Result result = Read(Minimal("[on]\n'a' = 'back'\n'b' = 'stop'\n'c' = { music = 'back', crossfade = 2 }\n"));
		REQUIRE(result.file);
		CHECK(result.file->on[0].target.kind == PLAYLIST_TARGET_BACK);
		CHECK(result.file->on[1].target.kind == PLAYLIST_TARGET_STOP);
		CHECK(result.file->on[2].target.kind == PLAYLIST_TARGET_BACK);
		CHECK(result.file->on[2].target.bMusic);
		CHECK(result.file->on[2].target.nCrossfade == 2);
	}

	SUBCASE("A table needs play or music, not both")
	{
		CHECK(Read(Minimal("next = { fade_in = 1 }\n")).HasError("needs play or music"));
		CHECK(Read(Minimal("next = { play = 'p', music = 'p' }\n")).HasError("both play and music"));
	}

	SUBCASE("crossfade can't be combined with the other fades")
	{
		CHECK(Read(Minimal("next = { play = 'p', crossfade = 1, fade_in = 1 }\n")).HasError("crossfade together with fade_out or fade_in"));
		CHECK(Read(Minimal("next = { play = 'p', crossfade = 1, fade_out = 1 }\n")).HasError("crossfade together with fade_out or fade_in"));

		Result result = Read(Minimal("next = { play = 'p', fade_out = 1, fade_in = 2 }\n"));
		REQUIRE(result.file);
		CHECK(result.file->Find("p")->next.nFadeOut == 1);
		CHECK(result.file->Find("p")->next.nFadeIn == 2);
	}

	SUBCASE("Other types aren't targets")
	{
		CHECK(Read(Minimal("next = 1\n")).HasError("next should be a playlist name"));
		CHECK(Read(Minimal("next = { play = { play = 'p' } }\n")).HasError("should be a playlist name"));
		CHECK(Read(Minimal("next = ''\n")).HasError("empty playlist name"));
	}
}

TEST_CASE("Unknown playlist names")
{
	SUBCASE("are errors in the base file")
	{
		Result result = Read(Minimal("next = 'q'\n[on]\n'a' = { music = 'r' }\n"));
		CHECK_FALSE(result.file);
		CHECK(result.HasError("no playlist named \"q\"", 4));
		CHECK(result.HasError("no playlist named \"r\"", 6));
	}

	SUBCASE("are expected from the base in a map's file")
	{
		Result result = Read(Minimal("next = 'q'\n[on]\n'a' = { music = 'q' }\n'b' = 'r'\n"), true);
		REQUIRE(result.file);
		CHECK(result.diagnostics.All().empty());
		REQUIRE(result.file->external.size() == 2);
		CHECK(result.file->external[0].first == "q");
		CHECK(result.file->external[0].second == 4);
		CHECK(result.file->external[1].first == "r");
	}
}

TEST_CASE("back and stop can't be playlist names")
{
	CHECK(Read("version = 1\n[playlist.back]\ntracks = ['a']\n").HasError("\"back\" can't be used as a playlist name", 2));
	CHECK(Read("version = 1\n[playlist.stop]\ntracks = ['a']\n").HasError("\"stop\" can't be used"));
	CHECK(Read("version = 1\n[playlist.\"\"]\ntracks = ['a']\n").HasError("empty name"));
}

TEST_CASE("Quoted playlist names work")
{
	Result result = Read("version = 1\n[playlist.\"my list\"]\ntracks = ['a']\n[on]\n'x' = 'my list'\n");
	REQUIRE(result.file);
	CHECK(result.file->Find("my list"));
}

TEST_CASE("on can be written as dotted keys or as a table")
{
	Result dotted = Read(Minimal("on.'music\\a.wav' = 'p'\non.'music\\b.wav' = 'p'\n"));
	REQUIRE(dotted.file);
	CHECK(dotted.file->Find("p")->on.size() == 2);

	Result table = Read(Minimal("[playlist.p.on]\n'music\\a.wav' = 'p'\n"));
	REQUIRE(table.file);
	REQUIRE(table.file->Find("p")->on.size() == 1);
	CHECK(table.file->Find("p")->on[0].nLine == 5);
}

TEST_CASE("Patterns that are the same once normalized are duplicates")
{
	Result result = Read(Minimal("[on]\n'MUSIC\\x.wav' = 'p'\n'music/X.WAV' = 'p'\n"));
	CHECK_FALSE(result.file);
	CHECK(result.HasError("same file pattern as on line", 6));
}

TEST_CASE("music patterns")
{
	Result result = Read("version = 1\nmusic = ['Music\\*', 'sound/music/*']\n");
	REQUIRE(result.file);
	CHECK(result.file->bHasMusic);
	REQUIRE(result.file->music.size() == 2);
	CHECK(result.file->music[0] == "music\\*");
	CHECK(result.file->music[1] == "sound\\music\\*");

	SUBCASE("The default covers every game")
	{
		REQUIRE(PlaylistDefaultMusic().size() == 2);
		CHECK(PlaylistDefaultMusic()[0] == "music\\*");
		CHECK(PlaylistDefaultMusic()[1] == "data\\global\\music\\*");
	}
}

TEST_CASE("map_playlists and inherit belong to the base and the map")
{
	Result base = Read("version = 1\nmap_playlists = false\n");
	REQUIRE(base.file);
	CHECK_FALSE(base.file->bMapPlaylists);
	CHECK(Read("version = 1\ninherit = false\n").HasError("inherit can only be set in a map's playlist file"));

	Result map = Read("version = 1\ninherit = false\n", true);
	REQUIRE(map.file);
	CHECK_FALSE(map.file->bInherit);
	CHECK(Read("version = 1\nmap_playlists = false\n", true).HasError("map_playlists can only be set in the base file"));
}

TEST_CASE("Paths in a file from an archive must stay relative to the game")
{
	const char *const lpszBad[] = { "C:\\music\\x.ogg", "\\x.ogg", "/x.ogg", "\\\\server\\x.ogg", "..\\x.ogg", "music\\..\\..\\x.ogg" };
	for (const char *lpszPath : lpszBad)
	{
		CAPTURE(lpszPath);
		std::string text = "version = 1\n[playlist.p]\ntracks = ['" + std::string(lpszPath) + "']\n";
		CHECK_FALSE(Read(text).file);
		CHECK_FALSE(Read(text, true).file);
		CHECK(Read(text, false, true).file);
	}

	SUBCASE("Patterns only match names, so they may be anything")
	{
		CHECK(Read(Minimal("[on]\n'C:\\StarCraft\\music\\*' = 'p'\n")).file);
	}
}

TEST_CASE("Over-long paths are errors")
{
	std::string longPath(PLAYLIST_MAX_PATH + 1, 'a');
	CHECK(Read("version = 1\n[playlist.p]\ntracks = ['" + longPath + "']\n").HasError("longer than"));
	CHECK(Read(Minimal("[on]\n'" + longPath + "' = 'p'\n")).HasError("longer than"));
	CHECK(Read("version = 1\n[playlist.p]\ntracks = ['" + std::string(PLAYLIST_MAX_PATH, 'a') + "']\n").file);
}

TEST_CASE("Every problem is reported, not just the first")
{
	Result result = Read("version = 1\nfoo = 1\n[playlist.p]\nbar = 2\ntracks = []\n[playlist.back]\ntracks = ['a']\n");
	CHECK_FALSE(result.file);
	CHECK(result.diagnostics.ErrorCount() == 4);
}

TEST_CASE("Diagnostics are formatted the same everywhere")
{
	CHECK(FormatPlaylistDiagnostic(PlaylistDiagnostic{PLAYLIST_ERROR, "music\\playlist.toml", 12, "oops"}) == "music\\playlist.toml:12: error: oops");
	CHECK(FormatPlaylistDiagnostic(PlaylistDiagnostic{PLAYLIST_WARNING, "x.toml", 0, "hm"}) == "x.toml: warning: hm");
}

TEST_CASE("A map's file has limits")
{
	SUBCASE("on its size")
	{
		std::string big = Minimal("# " + std::string(PLAYLIST_MAP_MAX_FILE_SIZE, 'x') + "\n");
		CHECK(Read(big, true).HasError("at most 64 KB"));
		CHECK(Read(big, false).file);
	}

	SUBCASE("on the number of playlists")
	{
		std::string text = "version = 1\n";
		for (int iPlaylist = 0; iPlaylist <= PLAYLIST_MAP_MAX_PLAYLISTS; iPlaylist++)
			text += "playlist.p" + std::to_string(iPlaylist) + ".tracks = ['a']\n";

		CHECK(Read(text, true).HasError("at most 256 playlists"));
		CHECK(Read(text, false).file);
	}

	SUBCASE("on the number of tracks")
	{
		std::string text = "version = 1\n[playlist.p]\ntracks = [";
		for (int iTrack = 0; iTrack <= PLAYLIST_MAP_MAX_TRACKS; iTrack++)
			text += "'a',";
		text += "]\n";

		CHECK(Read(text, true).HasError("tracks in all"));
		CHECK(Read(text, false).file);
	}

	SUBCASE("on the number of transitions")
	{
		std::string text = Minimal("[on]\n");
		for (int iTransition = 0; iTransition <= PLAYLIST_MAP_MAX_TRANSITIONS; iTransition++)
			text += "'f" + std::to_string(iTransition) + "' = 'p'\n";

		CHECK(Read(text, true).HasError("transitions in all"));
		CHECK(Read(text, false).file);
	}
}

TEST_CASE("Hostile input doesn't crash the reader")
{
	const std::string inputs[] =
	{
		std::string("\0\0\0\0", 4),
		"\xFF\xFE\x00v",
		std::string(10000, '['),
		"version = 1\n" + std::string(5000, '{'),
		"a = " + std::string(3000, '[') + std::string(3000, ']'),
		"version = 1\n[playlist.p]\ntracks = [{file='a', times=99999999999999999999}]\n",
		"version = 1\n[playlist.p]\ntracks = [{file='a', times=-9223372036854775808}]\n",
		"version = 1\n[[playlist]]\n",
		"version = 1\n[playlist.p]\ntracks = [[['a']]]\n",
		"version = 1\n'\xC3\x28' = 1\n",
	};

	for (const std::string &input : inputs)
	{
		CHECK_FALSE(Read(input, true).file);
		CHECK_FALSE(Read(input, false).file);
	}
}

TEST_CASE("Names a map uses are checked against the base when they are combined")
{
	Result base = Read("version = 1\n[playlist.boss]\ntracks = ['a']\n");
	Result map = Read("version = 1\n[on]\n'a' = { music = 'boss' }\n'b' = 'nothing'\n", true);
	REQUIRE(base.file);
	REQUIRE(map.file);

	PlaylistDiagnostics diagnostics;
	CheckPlaylistCombination(*map.file, base.file.get(), diagnostics);
	REQUIRE(diagnostics.All().size() == 1);
	CHECK(diagnostics.All()[0].severity == PLAYLIST_WARNING);
	CHECK(diagnostics.All()[0].nLine == 4);
	CHECK(diagnostics.All()[0].message.find("\"nothing\"") != std::string::npos);

	SUBCASE("Without the base, every name it expects is a warning")
	{
		PlaylistDiagnostics alone;
		CheckPlaylistCombination(*map.file, NULL, alone);
		CHECK(alone.WarningCount() == 2);
	}

	SUBCASE("A map that doesn't inherit can't use the base's playlists")
	{
		Result isolated = Read("version = 1\ninherit = false\n[on]\n'a' = { music = 'boss' }\n", true);
		REQUIRE(isolated.file);
		PlaylistDiagnostics alone;
		CheckPlaylistCombination(*isolated.file, base.file.get(), alone);
		CHECK(alone.WarningCount() == 1);
	}
}

TEST_CASE("Equally specific patterns that can match the same file are warned about")
{
	Result result = Read(Minimal("on.'a*' = 'p'\non.'*b' = 'p'\n[on]\n'music\\t?.wav' = 'p'\n'music\\*1.wav' = 'p'\n'music\\*.ogg' = 'p'\n'music\\t1.wav' = 'p'\n"));
	REQUIRE(result.file);

	PlaylistDiagnostics diagnostics;
	FindAmbiguousPlaylistPatterns(*result.file, diagnostics);
	// music\t?.wav and music\*1.wav both have 11 other characters and both match music\t1.wav; music\*.ogg can't match the same; music\t1.wav has no wildcards. The playlist's a* and *b overlap too.
	REQUIRE(diagnostics.All().size() == 2);
	CHECK(diagnostics.All()[0].message.find("'music\\*1.wav' wins") != std::string::npos);
	CHECK(diagnostics.All()[1].message.find("playlist \"p\": on") != std::string::npos);
}

TEST_CASE("Randomly damaged files don't crash the reader")
{
	const std::string original = "version = 1\nmusic = ['music\\*']\n[on]\n'music\\t?.wav' = { music = \"a\", fade_out = 3 }\n'x' = 'back'\n"
		"[playlist.a]\nplay = \"all\"\ntimes = 2\norder = \"shuffle\"\ncrossfade = 1.5\ntracks = ['a.ogg', { file = \"b.ogg\", times = 2 }]\nnext = { play = \"a\", crossfade = 2 }\non.'y' = \"stop\"\n";
	const char pieces[] = "[]{}'\"=.,\\\n #\t0x1e+-_:";

	// The same damage every run, so a failure can be repeated
	uint64_t nState = 12345;
	auto next = [&nState]() { nState = nState * 6364136223846793005ULL + 1442695040888963407ULL; return (size_t)(nState >> 33); };

	size_t nRead = 0;
	for (int iCase = 0; iCase < 3000; iCase++)
	{
		std::string text = original;
		for (size_t nChanges = 1 + next() % 4; nChanges > 0; nChanges--)
		{
			size_t iAt = next() % (text.size() + 1);
			switch (next() % 4)
			{
				case 0: text.insert(iAt, 1, pieces[next() % (sizeof(pieces) - 1)]); break;
				case 1: if (iAt < text.size()) text.erase(iAt, 1 + next() % 8); break;
				case 2: if (iAt < text.size()) text[iAt] = (char)(next() % 256); break;
				default: text.insert(iAt, std::string(1 + next() % 300, pieces[next() % (sizeof(pieces) - 1)])); break;
			}
		}

		PlaylistReadOptions options;
		options.label = "damaged";
		options.bMap = (iCase % 2) != 0;
		PlaylistDiagnostics diagnostics;
		std::shared_ptr<const PlaylistFile> file = ReadPlaylistFile(text, options, diagnostics);

		// Either it reads, or it says why not
		if (file)
			nRead++;
		else
			CHECK(diagnostics.HasErrors());
	}

	// Some damage is harmless, such as an extra space
	CHECK(nRead > 0);
}
