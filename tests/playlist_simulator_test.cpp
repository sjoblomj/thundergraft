// Tests of the simulator behind tgplaylist simulate, which runs the real player against a list of events

#include <doctest.h>

#include "fakes/SharedPtrStrings.h"

#include "PlaylistSimulator.h"

#include <string.h>
#include <map>

namespace
{
	std::shared_ptr<const PlaylistFile> Parse(const std::string &text, bool bMap = false)
	{
		PlaylistReadOptions options;
		options.label = bMap ? "map.toml" : "playlist.toml";
		options.bMap = bMap;
		PlaylistDiagnostics diagnostics;
		std::shared_ptr<const PlaylistFile> file = ReadPlaylistFile(text, options, diagnostics);
		INFO(diagnostics.Format());
		REQUIRE(file);
		return file;
	}

	struct Run
	{
		bool bOK;
		std::string output;

		bool Has(const std::string &text) const
		{ return output.find(text) != std::string::npos; }

		// Whether the lines appear in this order
		bool HasInOrder(std::initializer_list<const char *> lines) const
		{
			size_t iFrom = 0;
			for (const char *lpszLine : lines)
			{
				size_t iAt = output.find(lpszLine, iFrom);
				if (iAt == std::string::npos)
					return false;
				iFrom = iAt + strlen(lpszLine);
			}
			return true;
		}
	};

	Run Simulate(const std::string &base, const std::string &events, std::map<std::string, std::string> files = {}, uint64_t nSeed = 1)
	{
		PlaylistSimulation simulation;
		if (!base.empty())
			simulation.base = Parse(base);
		simulation.events = events;
		simulation.eventsLabel = "events.txt";
		simulation.nSeed = nSeed;
		simulation.readFile = [files](const std::string &path, std::string *lpText)
		{
			std::map<std::string, std::string>::const_iterator itFile = files.find(path);
			if (itFile == files.end())
				return false;
			*lpText = itFile->second;
			return true;
		};

		Run run;
		run.bOK = RunPlaylistSimulation(simulation, &run.output);
		INFO(run.output);
		return run;
	}

	const char *const BASE = R"(version = 1
[on]
'music\*' = "m"
'sound\cue.wav' = { music = "boss", crossfade = 2 }
[playlist.m]
tracks = ['m1.ogg', 'm2.ogg']
[playlist.boss]
play = "all"
tracks = ['b1.ogg', 'b2.ogg']
next = "back"
)";
}

TEST_CASE("Each event is echoed and followed by the state")
{
	Run run = Simulate(BASE, "start music\\title.wav\n");
	CHECK(run.bOK);
	CHECK(run.HasInOrder({
		"> start music\\title.wav\n",
		"  start 'music\\title.wav' in s1 (music): [on] 'music\\*' (playlist.toml:3) -> \"m\"\n",
		"  s1 plays 'm1.ogg' of \"m\"\n",
		"= current playlist: \"m\"; music: s1, 'm1.ogg' of \"m\"\n" }));
}

TEST_CASE("Comments and blank lines are skipped, and quotes keep spaces")
{
	Run run = Simulate(BASE, "# a comment\n\n   \nstart 'music\\my title.wav' as \"the music\"  # trailing\n");
	CHECK(run.bOK);
	CHECK(run.Has("> start 'music\\my title.wav' as 'the music'\n"));
	CHECK(run.Has("in the music (music)"));
}

TEST_CASE("Tracks end, visits end, and the game ends streams")
{
	Run run = Simulate(BASE, "start music\\a.wav\nend-track\nstop\nstart music\\b.wav\n");
	CHECK(run.bOK);
	CHECK(run.HasInOrder({
		"s1 plays 'm1.ogg'",
		"s1: the visit of \"m\" ends, and the game is told the stream ended",
		"s1 has ended, as far as the game can tell",
		"the game ends s1",
		"s2 plays 'm2.ogg'" }));
}

TEST_CASE("A cue, a visit of a whole playlist, and back")
{
	Run run = Simulate(BASE, "start music\\a.wav\nwait 65\nstart sound\\cue.wav as cue\nend-visit\n");
	CHECK(run.bOK);
	CHECK(run.HasInOrder({
		"start 'sound\\cue.wav' in cue: [on] 'sound\\cue.wav' (playlist.toml:4) -> { music = \"boss\", crossfade = 2 }",
		"the music (s1) plays 'b1.ogg' of \"boss\"",
		"> end-visit",
		"s1 plays 'b2.ogg' of \"boss\"",
		"s1: the visit of \"boss\" ends; next: \"back\"",
		"s1 goes back to 'm1.ogg' of \"m\" from 1:05",
		"= current playlist: \"m\"" }));
}

TEST_CASE("Time passing moves through tracks of the given lengths")
{
	Run run = Simulate("version = 1\n[on]\n'music\\*' = 'm'\n[playlist.m]\nplay = 'all'\ntracks = ['a.ogg', 'b.ogg']\n",
		"length a.ogg 10\nlength b.ogg 5\nstart music\\x.wav\nwait 9\nwait 2\nwait 5\n");
	CHECK(run.bOK);
	CHECK(run.HasInOrder({
		"> wait 9\n= current playlist: \"m\"; music: s1, 'a.ogg'",
		"> wait 2\n  s1 plays 'b.ogg'",
		"> wait 5\n  s1: the visit of \"m\" ends, and the game is told the stream ended\n  s1 has ended, as far as the game can tell" }));
}

TEST_CASE("Missing files")
{
	Run run = Simulate(BASE, "missing m1.ogg\nstart music\\x.wav\n");
	CHECK(run.bOK);
	CHECK(run.Has("playlist.toml:6: warning: playlist \"m\": track 'm1.ogg' can't be found, so it is skipped"));
	CHECK(run.Has("s1 plays 'm2.ogg'"));

	SUBCASE("The game's own file missing too means nothing plays")
	{
		Run nothing = Simulate("", "missing music\\x.wav\nstart music\\x.wav\n");
		CHECK(nothing.Has("s1 can't be played"));
	}
}

TEST_CASE("Looping streams and forever visits don't end")
{
	Run run = Simulate("version = 1\n[on]\n'music\\a.wav' = 'one'\n'music\\b.wav' = 'all'\n[playlist.one]\ntracks = ['o.ogg']\n[playlist.all]\nplay = 'all'\ntimes = 'forever'\ntracks = ['f.ogg']\n",
		"start music\\a.wav loop\nend-track\nstop\nstart music\\b.wav\nend-visit\n");
	CHECK(run.bOK);
	CHECK(run.Has("the track loops, so it never ends"));
	CHECK(run.Has("the visit of \"all\" never ends, as it plays forever"));
}

TEST_CASE("Maps are loaded and unloaded")
{
	std::map<std::string, std::string> files;
	files["map.toml"] = "version = 1\n[on]\n'music\\*' = 'mapmusic'\n[playlist.mapmusic]\ntracks = ['map.ogg']\n";
	files["broken.toml"] = "version = 1\nnonsense = 1\n";
	files["other.toml"] = "version = 1\n[on]\n'music\\*' = 'nowhere'\n";

	Run run = Simulate(BASE, "load-map map.toml as mymap\nstart music\\a.wav\nunload-map mymap\n", files);
	CHECK(run.bOK);
	CHECK(run.HasInOrder({
		"map loaded: map.toml",
		"s1 plays 'map.ogg' of \"mapmusic\"",
		"map unloaded: map.toml",
		"s1 starts again: [on] 'music\\*' (playlist.toml:3) -> \"m\"",
		"= current playlist: \"m\"; music: s1, 'm1.ogg' of \"m\"" }));

	SUBCASE("A broken map's file isn't used")
	{
		Run broken = Simulate(BASE, "load-map broken.toml\nstart music\\a.wav\n", files);
		CHECK(broken.Has("broken.toml:2: error: unknown key \"nonsense\""));
		CHECK(broken.Has("the map's playlist file has errors, so it isn't used"));
		CHECK(broken.Has("s1 plays 'm1.ogg' of \"m\""));
	}

	SUBCASE("Names neither file defines are warned about")
	{
		Run other = Simulate(BASE, "load-map other.toml\n", files);
		CHECK(other.Has("other.toml:3: warning: there is no playlist named \"nowhere\" in this file or in the base file"));
	}

	SUBCASE("Without a base file")
	{
		Run alone = Simulate("", "load-map map.toml\nstart music\\a.wav\n", files);
		CHECK(alone.Has("s1 plays 'map.ogg'"));
	}

	SUBCASE("When the base file turns them off")
	{
		Run off = Simulate("version = 1\nmap_playlists = false\n", "load-map map.toml\n", files);
		CHECK(off.Has("map_playlists = false"));
	}
}

TEST_CASE("Mistakes in the events are reported with their lines")
{
	SUBCASE("Unknown events")
	{
		Run run = Simulate(BASE, "start music\\a.wav\nfrobnicate\n");
		CHECK_FALSE(run.bOK);
		CHECK(run.Has("events.txt:2: unknown event frobnicate"));
	}

	SUBCASE("Bad arguments")
	{
		CHECK(Simulate(BASE, "start\n").Has("events.txt:1: start needs a file"));
		CHECK(Simulate(BASE, "start a.wav sideways\n").Has("unexpected sideways"));
		CHECK(Simulate(BASE, "wait soon\n").Has("wait takes a number of seconds"));
		CHECK(Simulate(BASE, "wait -1\n").Has("wait takes a number of seconds"));
		CHECK(Simulate(BASE, "length a.ogg\n").Has("length takes a file and a number of seconds"));
		CHECK(Simulate(BASE, "missing\n").Has("missing takes a file"));
		CHECK(Simulate(BASE, "start 'unclosed\n").Has("a quote isn't closed"));
		CHECK(Simulate(BASE, "load-map\n").Has("load-map takes a playlist file"));
		CHECK(Simulate(BASE, "load-map nothere.toml\n").Has("can't read nothere.toml"));
		CHECK(Simulate(BASE, "unload-map\n").Has("no map is loaded"));
	}

	SUBCASE("Streams that don't exist")
	{
		CHECK(Simulate(BASE, "end-track\n").Has("no stream is playing"));
		CHECK(Simulate(BASE, "start music\\a.wav\nend-track s9\n").Has("there is no stream called s9"));
		CHECK(Simulate(BASE, "start music\\a.wav as x\nstart music\\b.wav as x\n").Has("a stream called x is already playing"));
	}

	SUBCASE("The rest of the events still run")
	{
		Run run = Simulate(BASE, "nonsense\nstart music\\a.wav\n");
		CHECK_FALSE(run.bOK);
		CHECK(run.Has("s1 plays 'm1.ogg'"));
	}
}

TEST_CASE("Events are about the music stream unless named, or else the last one started")
{
	Run run = Simulate(BASE, "start music\\a.wav\nstart sound\\other.wav\nend-track\n");
	CHECK(run.HasInOrder({ "> end-track", "s1: the visit of \"m\" ends" }));

	Run noMusic = Simulate(BASE, "start sound\\x.wav\nend-track\n");
	CHECK(noMusic.Has("s1 has ended, as far as the game can tell"));
}

TEST_CASE("Shuffles repeat from the seed")
{
	const char *lpszShuffled = "version = 1\n[on]\n'music\\*' = 'm'\n[playlist.m]\norder = 'shuffle'\ntracks = ['1.ogg', '2.ogg', '3.ogg', '4.ogg', '5.ogg', '6.ogg']\n";
	std::string events;
	for (int i = 0; i < 6; i++)
		events += "start music\\a.wav\nend-track\nstop\n";

	CHECK(Simulate(lpszShuffled, events, {}, 5).output == Simulate(lpszShuffled, events, {}, 5).output);
	CHECK(Simulate(lpszShuffled, events, {}, 5).output != Simulate(lpszShuffled, events, {}, 6).output);
}

TEST_CASE("Ending a track that waits for a fade-out lets the fade-out finish first")
{
	Run run = Simulate("version = 1\n[on]\n'music\\*' = 'm'\n'cue.wav' = { music = 'boss', fade_out = 2.5 }\n[playlist.m]\ntracks = ['m.ogg']\n[playlist.boss]\ntracks = ['b.ogg']\nnext = 'back'\n",
		"start music\\a.wav\nstart cue.wav as cue\nend-visit\n");
	CHECK(run.bOK);
	CHECK(run.HasInOrder({
		"the music (s1) plays 'b.ogg' of \"boss\"",
		"> end-visit\n  (waiting 2.5 seconds for the music before it to fade out)",
		"s1: the visit of \"boss\" ends; next: \"back\"",
		"s1 goes back to 'm.ogg' of \"m\" from 0:00" }));
	CHECK_FALSE(run.Has("the track doesn't end"));
}
