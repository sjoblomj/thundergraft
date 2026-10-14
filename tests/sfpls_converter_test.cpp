// Tests of the converter for ShadowFlare's Playlist Plugin's playlist.txt files, and of finding the base playlist file

#include <doctest.h>

#include "fakes/SharedPtrStrings.h"

#include "PlaylistFile.h"
#include "PlaylistLoader.h"
#include "SfplsConverter.h"

#include <map>

namespace
{
	struct Conversion
	{
		bool bOK;
		std::string toml;
		PlaylistDiagnostics diagnostics;
		std::shared_ptr<const PlaylistFile> file;

		bool HasError(const std::string &text, unsigned nLine = 0) const
		{
			for (const PlaylistDiagnostic &diagnostic : diagnostics.All())
			{
				if (diagnostic.severity == PLAYLIST_ERROR && diagnostic.message.find(text) != std::string::npos && (!nLine || diagnostic.nLine == nLine))
					return true;
			}
			return false;
		}
	};

	// Converts, and reads the result back, which has to work whenever the conversion does
	Conversion Convert(const std::string &text)
	{
		Conversion conversion;
		conversion.bOK = ConvertSfplsPlaylist(text, "music\\playlist.txt", &conversion.toml, conversion.diagnostics);
		if (conversion.bOK)
		{
			PlaylistReadOptions options;
			options.label = "converted";
			PlaylistDiagnostics readDiagnostics;
			conversion.file = ReadPlaylistFile(conversion.toml, options, readDiagnostics);
			INFO(conversion.toml);
			INFO(readDiagnostics.Format());
			CHECK(conversion.file);
			CHECK(readDiagnostics.All().empty());
		}
		return conversion;
	}
}

TEST_CASE("The list format is told apart by its first line")
{
	CHECK(IsSfplsListFormat("<SFPlaylist1>\nmusic\\a.wav = a\n"));
	CHECK(IsSfplsListFormat("\n\n  <sfplaylist1>  \r\n"));
	CHECK(IsSfplsListFormat("\xEF\xBB\xBF<SFPlaylist1>\n"));
	CHECK_FALSE(IsSfplsListFormat("music\\a.wav\n<SFPlaylist1>\n"));
	CHECK_FALSE(IsSfplsListFormat(""));
}

TEST_CASE("A plain list of files becomes one playlist for all music")
{
	Conversion conversion = Convert("music\\title.wav\r\nmusic\\terran1.wav\r\n\r\nmusic\\zerg1.wav\r\n");
	REQUIRE(conversion.bOK);

	const char *lpszExpected =
		"# Converted from music\\playlist.txt\n"
		"version = 1\n"
		"\n"
		"[on]\n"
		"'music\\*'             = \"music\"    # Diablo, StarCraft, Warcraft II\n"
		"'data\\global\\music\\*' = \"music\"    # Diablo II\n"
		"\n"
		"[playlist.music]\n"
		"order = \"in order\"\n"
		"play = \"one\"\n"
		"tracks = [\n"
		"    'music\\title.wav',\n"
		"    'music\\terran1.wav',\n"
		"    'music\\zerg1.wav',\n"
		"]\n";
	CHECK(conversion.toml == lpszExpected);

	REQUIRE(conversion.file);
	const Playlist *lpMusic = conversion.file->Find("music");
	REQUIRE(lpMusic);
	CHECK(lpMusic->tracks.size() == 3);
	CHECK_FALSE(lpMusic->bShuffle);
	CHECK_FALSE(lpMusic->bPlayAll);
	CHECK_FALSE(lpMusic->bHasNext);
}

TEST_CASE("Named lists become playlists, and the mappings transitions")
{
	Conversion conversion = Convert(
		"<SFPlaylist1>\n"
		"music\\terran1.wav = terran\n"
		"music\\terran2.wav=terran\n"
		"  music\\zerg1.wav   =   zerg  \n"
		"\n"
		"<terran>\n"
		"music\\mymusic1.wav\n"
		"music\\mymusic2.wav\n"
		"<zerg>\n"
		"music/zergy.ogg\n");
	REQUIRE(conversion.bOK);

	const char *lpszExpected =
		"# Converted from music\\playlist.txt\n"
		"version = 1\n"
		"\n"
		"[on]\n"
		"'music\\terran1.wav' = \"terran\"\n"
		"'music\\terran2.wav' = \"terran\"\n"
		"'music\\zerg1.wav' = \"zerg\"\n"
		"\n"
		"[playlist.terran]\n"
		"order = \"in order\"\n"
		"play = \"one\"\n"
		"tracks = [\n"
		"    'music\\mymusic1.wav',\n"
		"    'music\\mymusic2.wav',\n"
		"]\n"
		"\n"
		"[playlist.zerg]\n"
		"order = \"in order\"\n"
		"play = \"one\"\n"
		"tracks = [\n"
		"    'music/zergy.ogg',\n"
		"]\n";
	CHECK(conversion.toml == lpszExpected);

	REQUIRE(conversion.file);
	CHECK(conversion.file->on.size() == 3);
	CHECK(conversion.file->Find("zerg")->tracks[0].file == "music\\zergy.ogg");
}

TEST_CASE("Mappings can replace sounds that aren't music too")
{
	Conversion conversion = Convert("<SFPlaylist1>\nstaredit\\wav\\report.wav = report\n<report>\nsound\\report2.ogg\n");
	REQUIRE(conversion.bOK);
	REQUIRE(conversion.file);
	CHECK(conversion.file->on[0].pattern == "staredit\\wav\\report.wav");
	CHECK_FALSE(conversion.file->on[0].target.bMusic);
}

TEST_CASE("List names TOML can't take bare are quoted")
{
	Conversion conversion = Convert("<SFPlaylist1>\nmusic\\a.wav = my \"list\"\n<my \"list\">\nmusic\\b.wav\n");
	REQUIRE(conversion.bOK);
	CHECK(conversion.toml.find("[playlist.\"my \\\"list\\\"\"]") != std::string::npos);
	REQUIRE(conversion.file);
	CHECK(conversion.file->Find("my \"list\""));
}

TEST_CASE("Paths with single quotes go in double quotes")
{
	Conversion conversion = Convert("music\\it's.wav\n");
	REQUIRE(conversion.bOK);
	CHECK(conversion.toml.find("\"music\\\\it's.wav\"") != std::string::npos);
	REQUIRE(conversion.file);
	CHECK(conversion.file->Find("music")->tracks[0].file == "music\\it's.wav");
}

TEST_CASE("Lists called back or stop are renamed, with a warning")
{
	Conversion conversion = Convert("<SFPlaylist1>\nmusic\\a.wav = back\nmusic\\b.wav = stop\n<back>\nmusic\\x.wav\n<stop>\nmusic\\y.wav\n<back_list>\nmusic\\z.wav\n");
	REQUIRE(conversion.bOK);
	CHECK(conversion.diagnostics.WarningCount() == 2);
	REQUIRE(conversion.file);
	CHECK(conversion.file->Find("back_list_"));
	CHECK(conversion.file->Find("stop_list"));
	CHECK(conversion.file->Find("back_list"));
	CHECK(conversion.file->on[0].target.playlist == "back_list_");
	CHECK(conversion.file->on[1].target.playlist == "stop_list");
}

TEST_CASE("Problems are reported with their lines, and stop the conversion")
{
	SUBCASE("A mapping to a list that doesn't exist")
	{
		Conversion conversion = Convert("<SFPlaylist1>\nmusic\\a.wav = nothing\n<other>\nmusic\\b.wav\n");
		CHECK_FALSE(conversion.bOK);
		CHECK(conversion.HasError("no list <nothing>", 2));
	}

	SUBCASE("A list with no files")
	{
		Conversion conversion = Convert("<SFPlaylist1>\nmusic\\a.wav = empty\n<empty>\n<other>\nmusic\\b.wav\n");
		CHECK_FALSE(conversion.bOK);
		CHECK(conversion.HasError("<empty> has no files", 3));
	}

	SUBCASE("A line that is neither a mapping nor a file")
	{
		Conversion conversion = Convert("<SFPlaylist1>\nmusic\\a.wav\n<a>\nmusic\\b.wav\n");
		CHECK_FALSE(conversion.bOK);
		CHECK(conversion.HasError("neither a mapping", 2));
	}

	SUBCASE("A mapping without both sides")
	{
		CHECK(Convert("<SFPlaylist1>\n= a\n<a>\nx\n").HasError("needs a file and a list name", 2));
		CHECK(Convert("<SFPlaylist1>\nx =\n<a>\nx\n").HasError("needs a file and a list name", 2));
	}

	SUBCASE("The same file mapped twice")
	{
		Conversion conversion = Convert("<SFPlaylist1>\nmusic\\a.wav = a\nMUSIC/A.WAV = a\n<a>\nx\n");
		CHECK_FALSE(conversion.bOK);
		CHECK(conversion.HasError("already mapped on line 2", 3));
	}

	SUBCASE("A list started twice")
	{
		Conversion conversion = Convert("<SFPlaylist1>\nmusic\\a.wav = a\n<a>\nx\n<a>\ny\n");
		CHECK_FALSE(conversion.bOK);
		CHECK(conversion.HasError("already started on line 3", 5));
	}

	SUBCASE("A list with an empty name")
	{
		CHECK(Convert("<SFPlaylist1>\n<>\nx\n").HasError("empty name", 2));
	}

	SUBCASE("A file that lists nothing")
	{
		CHECK(Convert("").HasError("lists no files"));
		CHECK(Convert("\r\n  \n").HasError("lists no files"));
	}

	SUBCASE("Control characters")
	{
		CHECK(Convert("music\\a\x01.wav\n").HasError("control character", 1));
	}

	SUBCASE("Every problem is reported")
	{
		Conversion conversion = Convert("<SFPlaylist1>\nmusic\\a.wav = nothing\nmusic\\b.wav\n<empty>\n");
		CHECK(conversion.diagnostics.ErrorCount() == 3);
	}
}

TEST_CASE("Old Mac line endings and tabs work")
{
	Conversion conversion = Convert("<SFPlaylist1>\r\tmusic\\a.wav\t=\ta\r<a>\r\tmusic\\b.wav\t\r");
	REQUIRE(conversion.bOK);
	REQUIRE(conversion.file);
	CHECK(conversion.file->on[0].pattern == "music\\a.wav");
	CHECK(conversion.file->Find("a")->tracks[0].file == "music\\b.wav");
}

namespace
{
	// Archives and a disk made of maps from names to contents
	class FakeSources : public IPlaylistSources
	{
	public:
		std::map<std::string, std::string> archives;
		std::map<std::string, std::string> disk;

		virtual bool ReadFromArchives(const std::string &name, size_t nMaxSize, std::string *lpText)
		{
			if (!archives.count(name))
				return false;
			*lpText = archives[name].size() <= nMaxSize ? archives[name] : "";
			return true;
		}

		virtual bool ReadFromDisk(const std::string &name, size_t nMaxSize, std::string *lpText, std::string *lpLabel)
		{
			if (!disk.count(name))
				return false;
			*lpText = disk[name].size() <= nMaxSize ? disk[name] : "";
			*lpLabel = "C:\\Game\\" + name;
			return true;
		}
	};

	const char *const TOML_A = "version = 1\n[playlist.a]\ntracks = ['C:\\x.ogg']\n";
	const char *const TOML_B = "version = 1\n[playlist.b]\ntracks = ['music\\x.ogg']\n";
}

TEST_CASE("The base file is looked for in the archives first, then on disk, TOML before playlist.txt")
{
	FakeSources sources;
	PlaylistDiagnostics diagnostics;
	bool bFound = true;

	SUBCASE("Nothing anywhere")
	{
		CHECK_FALSE(LoadBasePlaylist(sources, diagnostics, &bFound));
		CHECK_FALSE(bFound);
		CHECK(diagnostics.All().empty());
	}

	SUBCASE("The archives' TOML file beats everything")
	{
		sources.archives["music\\playlist.toml"] = TOML_B;
		sources.archives["music\\playlist.txt"] = "music\\x.wav\n";
		sources.disk["music\\playlist.toml"] = TOML_A;
		std::shared_ptr<const PlaylistFile> file = LoadBasePlaylist(sources, diagnostics, &bFound);
		REQUIRE(file);
		CHECK(file->Find("b"));
		CHECK(file->label == "music\\playlist.toml");
		CHECK_FALSE(file->bFromDisk);
	}

	SUBCASE("The archives' playlist.txt beats the disk's TOML file")
	{
		sources.archives["music\\playlist.txt"] = "<SFPlaylist1>\nmusic\\a.wav = l\n<l>\nmusic\\x.ogg\n";
		sources.disk["music\\playlist.toml"] = TOML_A;
		std::shared_ptr<const PlaylistFile> file = LoadBasePlaylist(sources, diagnostics, &bFound);
		REQUIRE(file);
		CHECK(file->Find("l"));
		CHECK(file->label == "music\\playlist.txt (converted)");
	}

	SUBCASE("A file on disk may name files anywhere on disk")
	{
		sources.disk["music\\playlist.toml"] = TOML_A;
		std::shared_ptr<const PlaylistFile> file = LoadBasePlaylist(sources, diagnostics, &bFound);
		REQUIRE(file);
		CHECK(file->bFromDisk);
		CHECK(file->label == "C:\\Game\\music\\playlist.toml");
	}

	SUBCASE("But one in an archive may not")
	{
		sources.archives["music\\playlist.toml"] = TOML_A;
		CHECK_FALSE(LoadBasePlaylist(sources, diagnostics, &bFound));
		CHECK(bFound);
		CHECK(diagnostics.HasErrors());
	}

	SUBCASE("playlist.txt on disk is the last resort")
	{
		sources.disk["music\\playlist.txt"] = "music\\x.wav\n";
		std::shared_ptr<const PlaylistFile> file = LoadBasePlaylist(sources, diagnostics, &bFound);
		REQUIRE(file);
		CHECK(file->Find("music"));
	}

	SUBCASE("A broken file isn't replaced by the next one")
	{
		sources.archives["music\\playlist.toml"] = "version = 1\nnonsense = 1\n";
		sources.disk["music\\playlist.toml"] = TOML_A;
		CHECK_FALSE(LoadBasePlaylist(sources, diagnostics, &bFound));
		CHECK(bFound);
		CHECK(diagnostics.ErrorCount() == 1);
		CHECK(diagnostics.All()[0].file == "music\\playlist.toml");
	}

	SUBCASE("A playlist.txt that can't be converted cleanly is an error")
	{
		sources.archives["music\\playlist.txt"] = "<SFPlaylist1>\nmusic\\a.wav = nothing\n";
		CHECK_FALSE(LoadBasePlaylist(sources, diagnostics, &bFound));
		CHECK(bFound);
		CHECK(diagnostics.All()[0].file == "music\\playlist.txt");
	}

	SUBCASE("Empty and over-sized files are errors")
	{
		sources.archives["music\\playlist.toml"] = "";
		CHECK_FALSE(LoadBasePlaylist(sources, diagnostics, &bFound));
		CHECK(bFound);
		CHECK(diagnostics.HasErrors());
	}
}
