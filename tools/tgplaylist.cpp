/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	tgplaylist: checks, converts and simulates ThunderGraft playlist files, without starting a game. See docs/playlists.md.

	It uses the same code as the plugin, so it finds the same problems, in the same words, as the plugin writes to ThunderGraft.log.
*/

#include <stdio.h>
#include <string.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <StormLib.h>

#include "PlaylistFile.h"
#include "PlaylistLoader.h"
#include "PlaylistPath.h"
#include "PlaylistSimulator.h"
#include "SfplsConverter.h"

#ifndef THUNDERGRAFT_VERSION
#define THUNDERGRAFT_VERSION "development build"
#endif

namespace
{
	const int EXIT_OK = 0;
	// The playlist has errors, or something couldn't be done
	const int EXIT_PROBLEMS = 1;
	// The command line is wrong
	const int EXIT_USAGE = 2;

	const char *const USAGE =
		"tgplaylist " THUNDERGRAFT_VERSION ": checks, converts and simulates ThunderGraft playlist files\n"
		"\n"
		"  tgplaylist check [options] <playlist.toml>\n"
		"      Reports every problem in a playlist file, with line numbers. A playlist.txt\n"
		"      in the older format is converted first, as ThunderGraft would.\n"
		"  tgplaylist check --map <map.toml> [--base <playlist.toml>] [options]\n"
		"      Checks a StarCraft map's playlist file, and lists the playlists it expects\n"
		"      the base file to provide; with --base, checks the two together.\n"
		"      Options for check and simulate:\n"
		"        --on-disk           The base file goes in the game's directory rather than\n"
		"                            in an archive, so its tracks may be anywhere on disk\n"
		"      Options for check only:\n"
		"        --dir <directory>   Also check that the tracks exist under this directory,\n"
		"                            such as the game's or an extracted archive's\n"
		"        --mpq <archive>     Also check that the tracks exist in this archive\n"
		"      --dir and --mpq can be given several times; a track has to be in one of them.\n"
		"\n"
		"  tgplaylist convert <playlist.txt> [<playlist.toml>]\n"
		"      Converts a playlist.txt in the older format to a playlist.toml, printing it\n"
		"      if no output file is given.\n"
		"\n"
		"  tgplaylist simulate [--seed <number>] [--on-disk] <playlist.toml> <events.txt>\n"
		"      Plays a list of events, such as the game starting music, and prints what\n"
		"      plays after each. The events, one per line:\n"
		"        start <file> [loop] [as <name>]   the game starts a stream of the file\n"
		"        end-track [<stream>]              the playing track plays to its end\n"
		"        end-visit [<stream>]              the playlist's visit plays to its end\n"
		"        stop [<stream>]                   the game ends the stream\n"
		"        wait <seconds>                    time passes\n"
		"        length <file> <seconds>           how long a file is (120 unless given)\n"
		"        missing <file>                    the file can't be found\n"
		"        load-map <map.toml> [as <name>]   a map with this playlist file is loaded\n"
		"        unload-map [<name>]               the map is unloaded\n"
		"\n"
		"Exit status: 0 if all is well (there may be warnings), 1 if there are errors,\n"
		"2 if the command line is wrong.\n";

	bool ReadFile(const std::string &path, std::string *lpText)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return false;

		std::ostringstream contents;
		contents << file.rdbuf();
		*lpText = contents.str();
		return !file.bad();
	}

	bool EndsWith(const std::string &text, const std::string &end)
	{
		return text.size() >= end.size() && NormalizePlaylistName(text.substr(text.size() - end.size())) == end;
	}

	int Usage(const std::string &problem)
	{
		fprintf(stderr, "tgplaylist: %s\n\n%s", problem.c_str(), USAGE);
		return EXIT_USAGE;
	}

	void Print(const PlaylistDiagnostics &diagnostics)
	{
		fputs(diagnostics.Format().c_str(), stdout);
	}

	// Reads a playlist file of either format, as the plugin would
	std::shared_ptr<const PlaylistFile> Load(const std::string &path, bool bMap, bool bOnDisk, PlaylistDiagnostics &diagnostics, bool *lpbReadable)
	{
		std::string text;
		*lpbReadable = ReadFile(path, &text);
		if (!*lpbReadable)
			return nullptr;

		PlaylistReadOptions options;
		options.label = path;
		options.bMap = bMap;
		// A file in an archive may only name files relative to the game; one on disk may name any file
		options.bFromDisk = bOnDisk && !bMap;

		bool bSfpls = !bMap && EndsWith(path, ".txt");
		if (!bMap && text.size() > PLAYLIST_MAX_FILE_SIZE)
		{
			diagnostics.Error(path, 0, "the file is bigger than " + std::to_string(PLAYLIST_MAX_FILE_SIZE / (1024 * 1024)) + " MB");
			return nullptr;
		}

		return ReadAnyPlaylistFile(text, bSfpls, options, diagnostics);
	}

	// Where to look for tracks
	class CTrackFinder
	{
	private:
		std::vector<std::filesystem::path> m_directories;
		std::vector<HANDLE> m_archives;

		// Finds the path under the directory, ignoring case, as Windows would
		static bool FindInDirectory(const std::filesystem::path &directory, const std::string &path)
		{
			std::filesystem::path current = directory;
			std::string normalized = NormalizePlaylistName(path);

			size_t iStart = 0;
			while (iStart <= normalized.size())
			{
				size_t iEnd = normalized.find('\\', iStart);
				if (iEnd == std::string::npos)
					iEnd = normalized.size();

				std::string component = normalized.substr(iStart, iEnd - iStart);
				iStart = iEnd + 1;
				if (component.empty() || component == ".")
					continue;

				std::error_code error;
				bool bFound = false;
				for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(current, error))
				{
					if (NormalizePlaylistName(entry.path().filename().string()) == component)
					{
						current = entry.path();
						bFound = true;
						break;
					}
				}

				if (!bFound)
					return false;
			}

			std::error_code error;
			return std::filesystem::is_regular_file(current, error);
		}

	public:
		~CTrackFinder()
		{
			for (HANDLE hArchive : m_archives)
				SFileCloseArchive(hArchive);
		}

		bool Empty() const
		{ return m_directories.empty() && m_archives.empty(); }

		bool AddDirectory(const std::string &path)
		{
			std::error_code error;
			if (!std::filesystem::is_directory(path, error))
				return false;

			m_directories.push_back(path);
			return true;
		}

		bool AddArchive(const std::string &path)
		{
			HANDLE hArchive = NULL;
			if (!SFileOpenArchive(path.c_str(), 0, MPQ_OPEN_READ_ONLY, &hArchive))
				return false;

			m_archives.push_back(hArchive);
			return true;
		}

		// 1 if found, 0 if not, -1 if it can't be checked
		int Find(const std::string &path) const
		{
			if (IsAbsolutePlaylistPath(path))
			{
#ifdef _WIN32
				std::error_code error;
				return std::filesystem::is_regular_file(path, error) ? 1 : 0;
#else
				return -1;
#endif
			}

			for (HANDLE hArchive : m_archives)
			{
				if (SFileHasFile(hArchive, PlaylistPathForStorm(path).c_str()))
					return 1;
			}
			for (const std::filesystem::path &directory : m_directories)
			{
				if (FindInDirectory(directory, path))
					return 1;
			}

			return 0;
		}
	};

	void CheckTracks(const PlaylistFile &file, const CTrackFinder &finder, PlaylistDiagnostics &diagnostics, size_t *lpnUnchecked)
	{
		std::set<std::string> checked;
		for (const std::pair<const std::string, Playlist> &playlist : file.playlists)
		{
			for (const PlaylistTrack &track : playlist.second.tracks)
			{
				if (!checked.insert(NormalizePlaylistName(track.file)).second)
					continue;

				int nFound = finder.Find(track.file);
				if (nFound < 0)
					(*lpnUnchecked)++;
				else if (!nFound)
					diagnostics.Warning(file.label, track.nLine, "playlist \"" + playlist.first + "\": track " + QuotePlaylistPath(track.file) + " isn't in the given directories or archives");
			}
		}
	}

	int Check(const std::vector<std::string> &args)
	{
		std::string playlistPath, mapPath, basePath;
		CTrackFinder finder;
		bool bOnDisk = false;

		for (size_t iArg = 0; iArg < args.size(); iArg++)
		{
			const std::string &arg = args[iArg];
			bool bHasValue = iArg + 1 < args.size();
			if (arg == "--map" || arg == "--base" || arg == "--dir" || arg == "--mpq")
			{
				if (!bHasValue)
					return Usage(arg + " needs a value");

				const std::string &value = args[++iArg];
				if (arg == "--map")
					mapPath = value;
				else if (arg == "--base")
					basePath = value;
				else if (arg == "--dir" && !finder.AddDirectory(value))
				{
					fprintf(stderr, "tgplaylist: %s isn't a directory\n", value.c_str());
					return EXIT_PROBLEMS;
				}
				else if (arg == "--mpq" && !finder.AddArchive(value))
				{
					fprintf(stderr, "tgplaylist: can't open the archive %s\n", value.c_str());
					return EXIT_PROBLEMS;
				}
			}
			else if (arg == "--on-disk")
				bOnDisk = true;
			else if (arg.compare(0, 2, "--") == 0)
				return Usage("unknown option " + arg);
			else if (playlistPath.empty())
				playlistPath = arg;
			else
				return Usage("check takes one playlist file");
		}

		if (!mapPath.empty() && !playlistPath.empty())
			return Usage("with --map, the base file is given with --base");
		if (mapPath.empty() && playlistPath.empty())
			return Usage("check needs a playlist file");
		if (mapPath.empty() && !basePath.empty())
			return Usage("--base only goes with --map");

		PlaylistDiagnostics diagnostics;
		size_t nUnchecked = 0;
		bool bReadable;

		std::shared_ptr<const PlaylistFile> base, map;
		if (!mapPath.empty())
		{
			map = Load(mapPath, true, false, diagnostics, &bReadable);
			if (!bReadable)
			{
				fprintf(stderr, "tgplaylist: can't read %s\n", mapPath.c_str());
				return EXIT_PROBLEMS;
			}
		}
		if (!playlistPath.empty() || !basePath.empty())
		{
			const std::string &path = playlistPath.empty() ? basePath : playlistPath;
			base = Load(path, false, bOnDisk, diagnostics, &bReadable);
			if (!bReadable)
			{
				fprintf(stderr, "tgplaylist: can't read %s\n", path.c_str());
				return EXIT_PROBLEMS;
			}
		}

		for (const std::shared_ptr<const PlaylistFile> &file : { base, map })
		{
			if (!file)
				continue;

			FindAmbiguousPlaylistPatterns(*file, diagnostics);
			if (!finder.Empty())
				CheckTracks(*file, finder, diagnostics, &nUnchecked);
		}

		if (map && !basePath.empty() && base)
		{
			if (!base->bMapPlaylists)
				diagnostics.Warning(base->label, 0, "map_playlists = false, so ThunderGraft won't use the map's playlist file");
			CheckPlaylistCombination(*map, base.get(), diagnostics);
		}

		Print(diagnostics);

		if (map && basePath.empty() && !map->external.empty())
		{
			printf("The map's file uses these playlists, which the base file has to provide%s:\n", map->bInherit ? "" : " (but it has inherit = false, so the base file is ignored)");
			for (const std::pair<std::string, unsigned> &name : map->external)
				printf("  \"%s\" (line %u)\n", name.first.c_str(), name.second);
		}

		if (nUnchecked)
			printf("%zu track%s with absolute paths couldn't be checked, as this isn't Windows\n", nUnchecked, nUnchecked == 1 ? "" : "s");

		size_t nErrors = diagnostics.ErrorCount(), nWarnings = diagnostics.WarningCount();
		if (!nErrors && !nWarnings)
			printf("No problems found.\n");
		else
			printf("%zu error%s, %zu warning%s.%s\n", nErrors, nErrors == 1 ? "" : "s", nWarnings, nWarnings == 1 ? "" : "s",
				nErrors ? " ThunderGraft won't use a file with errors." : "");

		return nErrors ? EXIT_PROBLEMS : EXIT_OK;
	}

	int Convert(const std::vector<std::string> &args)
	{
		if (args.empty() || args.size() > 2)
			return Usage("convert takes a playlist.txt, and optionally the playlist.toml to write");

		std::string text;
		if (!ReadFile(args[0], &text))
		{
			fprintf(stderr, "tgplaylist: can't read %s\n", args[0].c_str());
			return EXIT_PROBLEMS;
		}

		PlaylistDiagnostics diagnostics;
		std::string toml;
		bool bConverted = ConvertSfplsPlaylist(text, args[0], &toml, diagnostics);
		fputs(diagnostics.Format().c_str(), stderr);
		if (!bConverted)
		{
			fprintf(stderr, "The file can't be converted cleanly, so nothing was written.\n");
			return EXIT_PROBLEMS;
		}

		if (args.size() < 2 || args[1] == "-")
		{
			fputs(toml.c_str(), stdout);
			return EXIT_OK;
		}

		std::ofstream output(args[1], std::ios::binary);
		output << toml;
		output.close();
		if (!output)
		{
			fprintf(stderr, "tgplaylist: can't write %s\n", args[1].c_str());
			return EXIT_PROBLEMS;
		}

		printf("Wrote %s.\n", args[1].c_str());
		return EXIT_OK;
	}

	int Simulate(const std::vector<std::string> &args)
	{
		PlaylistSimulation simulation;
		std::vector<std::string> paths;
		bool bOnDisk = false;

		for (size_t iArg = 0; iArg < args.size(); iArg++)
		{
			if (args[iArg] == "--seed")
			{
				if (iArg + 1 >= args.size())
					return Usage("--seed needs a number");

				char *lpEnd = NULL;
				simulation.nSeed = strtoull(args[++iArg].c_str(), &lpEnd, 10);
				if (*lpEnd || args[iArg].empty())
					return Usage("--seed needs a number");
			}
			else if (args[iArg] == "--on-disk")
				bOnDisk = true;
			else if (args[iArg].compare(0, 2, "--") == 0)
				return Usage("unknown option " + args[iArg]);
			else
				paths.push_back(args[iArg]);
		}

		if (paths.size() != 2)
			return Usage("simulate takes a playlist file and a file of events");

		PlaylistDiagnostics diagnostics;
		bool bReadable;
		simulation.base = Load(paths[0], false, bOnDisk, diagnostics, &bReadable);
		if (!bReadable)
		{
			fprintf(stderr, "tgplaylist: can't read %s\n", paths[0].c_str());
			return EXIT_PROBLEMS;
		}

		Print(diagnostics);
		if (!simulation.base)
		{
			printf("The playlist file has errors, so ThunderGraft wouldn't use it.\n");
			return EXIT_PROBLEMS;
		}

		if (!ReadFile(paths[1], &simulation.events))
		{
			fprintf(stderr, "tgplaylist: can't read %s\n", paths[1].c_str());
			return EXIT_PROBLEMS;
		}

		simulation.eventsLabel = paths[1];
		simulation.readFile = ReadFile;

		std::string output;
		bool bOK = RunPlaylistSimulation(simulation, &output);
		fputs(output.c_str(), stdout);

		return bOK ? EXIT_OK : EXIT_PROBLEMS;
	}
}

int main(int argc, char **argv)
{
	if (argc < 2)
		return Usage("no command given");

	std::string command = argv[1];
	std::vector<std::string> args(argv + 2, argv + argc);

	if (command == "--help" || command == "-h" || command == "help")
	{
		fputs(USAGE, stdout);
		return EXIT_OK;
	}
	if (command == "--version")
	{
		printf("tgplaylist %s\n", THUNDERGRAFT_VERSION);
		return EXIT_OK;
	}
	if (command == "check")
		return Check(args);
	if (command == "convert")
		return Convert(args);
	if (command == "simulate")
		return Simulate(args);

	return Usage("unknown command " + command);
}
