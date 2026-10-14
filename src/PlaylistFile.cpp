/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

#include "PlaylistFile.h"
#include "PlaylistPath.h"

#include <math.h>
#include <stdio.h>
#include <set>

// toml++ reports errors through return values instead of exceptions, and isn't asked to write TOML
#define TOML_EXCEPTIONS 0
#define TOML_ENABLE_FORMATTERS 0
// toml++ assumes GCC can't parse floats with std::from_chars, which it can since GCC 11, and falls back to string streams. Those bring in locales, whose thread-safe initialization needs functions Windows XP lacks.
#define TOML_FLOAT_CHARCONV 1
/*
	toml++ 3.4.0 asserts some things about its input before checking them properly, such as that a table header "[[[" goes on with a key. In debug builds that assertion aborts the game, and with NDEBUG it becomes a hint to the compiler that the condition holds, which makes hostile input (a map's playlist file) undefined behaviour. Its header only lets the assertion itself be replaced, and only without NDEBUG, so it is included without NDEBUG and with assertions that do nothing; the checks that follow them report the error.
*/
#pragma push_macro("NDEBUG")
#undef NDEBUG
#define TOML_ASSERT(expr) static_cast<void>(0)
#include <toml.hpp>
#pragma pop_macro("NDEBUG")

namespace
{
	std::string FormatSeconds(double nSeconds)
	{
		char szSeconds[32];
		snprintf(szSeconds, sizeof(szSeconds), "%g", nSeconds);
		return szSeconds;
	}

	std::string Quote(const std::string &text)
	{
		return "\"" + text + "\"";
	}

	std::string Str(const toml::key &key)
	{
		return std::string(key.str());
	}

	unsigned LineOf(const toml::node &node)
	{
		return node.source().begin.line;
	}

	unsigned LineOf(const toml::key &key)
	{
		return key.source().begin.line;
	}

	// How a value of the wrong type is described in messages
	std::string TypeName(const toml::node &node)
	{
		switch (node.type())
		{
			case toml::node_type::table: return "a table";
			case toml::node_type::array: return "a list";
			case toml::node_type::string: return "a string";
			case toml::node_type::integer: return "a whole number";
			case toml::node_type::floating_point: return "a number";
			case toml::node_type::boolean: return "true or false";
			default: return "a date or time";
		}
	}

	class CReader
	{
	private:
		const PlaylistReadOptions &m_options;
		PlaylistDiagnostics &m_diagnostics;
		std::shared_ptr<PlaylistFile> m_lpFile;
		bool m_bErrors;

		// Every reference to a playlist by name, to check once all playlists are known
		std::vector<std::pair<std::string, unsigned>> m_references;

		size_t m_nTracks;
		size_t m_nTransitions;

		void Error(unsigned nLine, const std::string &message)
		{
			m_diagnostics.Error(m_options.label, nLine, message);
			m_bErrors = true;
		}

		void WrongType(const std::string &what, const toml::node &node, const std::string &expected)
		{
			Error(LineOf(node), what + " should be " + expected + ", not " + TypeName(node));
		}

		// Checks a path or pattern; bTrack for files ThunderGraft opens itself, rather than patterns of files the game opens
		bool CheckPath(const std::string &path, unsigned nLine, const std::string &what, bool bTrack)
		{
			if (path.empty())
			{
				Error(nLine, what + " is empty");
				return false;
			}
			if (HasPlaylistControlCharacter(path))
			{
				Error(nLine, what + " contains a tab, newline or other control character. In double quotes, \\ starts an escape sequence such as \\t; write paths in single quotes, as in 'music\\terran1.wav', or with / instead of \\");
				return false;
			}
			if (path.size() > PLAYLIST_MAX_PATH)
			{
				Error(nLine, what + " is longer than " + std::to_string(PLAYLIST_MAX_PATH) + " characters");
				return false;
			}
			if (bTrack && !m_options.bFromDisk)
			{
				// Storm falls back to opening files on disk, so a file read from an archive mustn't be able to name any file on the computer
				if (IsAbsolutePlaylistPath(path))
				{
					Error(nLine, what + " " + QuotePlaylistPath(path) + " is an absolute path; a playlist file in an archive may only name files relative to the game, such as 'music\\title.ogg'");
					return false;
				}
				if (HasPlaylistParentReference(path))
				{
					Error(nLine, what + " " + QuotePlaylistPath(path) + " contains \"..\", which a playlist file in an archive may not use");
					return false;
				}
			}

			return true;
		}

		bool ReadSeconds(const toml::node &node, const std::string &what, double *lpnSeconds)
		{
			double nSeconds;
			if (const toml::value<int64_t> *lpInteger = node.as_integer())
				nSeconds = (double)lpInteger->get();
			else if (const toml::value<double> *lpFloat = node.as_floating_point())
				nSeconds = lpFloat->get();
			else
			{
				WrongType(what, node, "a number of seconds, such as 1.5");
				return false;
			}

			if (!isfinite(nSeconds) || nSeconds < 0 || nSeconds > PLAYLIST_MAX_SECONDS)
			{
				Error(LineOf(node), what + " should be a number of seconds from 0 to " + std::to_string(PLAYLIST_MAX_SECONDS));
				return false;
			}

			*lpnSeconds = nSeconds;
			return true;
		}

		bool ReadTargetName(const toml::node &node, const std::string &what, PlaylistTarget &target)
		{
			const toml::value<std::string> *lpString = node.as_string();
			if (!lpString)
			{
				WrongType(what, node, "a playlist name, \"back\" or \"stop\"");
				return false;
			}

			const std::string &name = lpString->get();
			if (name == "back")
				target.kind = PLAYLIST_TARGET_BACK;
			else if (name == "stop")
				target.kind = PLAYLIST_TARGET_STOP;
			else if (name.empty())
			{
				Error(LineOf(node), what + " is an empty playlist name");
				return false;
			}
			else
			{
				target.kind = PLAYLIST_TARGET_PLAYLIST;
				target.playlist = name;
				m_references.push_back(std::make_pair(name, LineOf(node)));
			}

			return true;
		}

		// A target: "name", "back", "stop", or { play = ..., fades } or { music = ..., fades }
		bool ReadTarget(const toml::node &node, const std::string &what, PlaylistTarget &target)
		{
			target.nLine = LineOf(node);

			if (node.is_string())
				return ReadTargetName(node, what, target);

			const toml::table *lpTable = node.as_table();
			if (!lpTable)
			{
				WrongType(what, node, "a playlist name, \"back\", \"stop\", or a table such as { music = \"boss\", fade_out = 3 }");
				return false;
			}

			bool bHasPlay = false, bHasMusic = false, bHasFadeOut = false, bHasFadeIn = false, bHasCrossfade = false, bOK = true;
			for (auto &&[key, value] : *lpTable)
			{
				std::string name = Str(key);
				if (name == "play" || name == "music")
				{
					if (bHasPlay || bHasMusic)
					{
						Error(LineOf(key), what + " has both play and music; use one of them");
						bOK = false;
						continue;
					}

					(name == "play" ? bHasPlay : bHasMusic) = true;
					target.bMusic = (name == "music");
					bOK = ReadTargetName(value, what + ": " + name, target) && bOK;
				}
				else if (name == "fade_out")
				{
					bHasFadeOut = true;
					bOK = ReadSeconds(value, what + ": fade_out", &target.nFadeOut) && bOK;
				}
				else if (name == "fade_in")
				{
					bHasFadeIn = true;
					bOK = ReadSeconds(value, what + ": fade_in", &target.nFadeIn) && bOK;
				}
				else if (name == "crossfade")
				{
					bHasCrossfade = true;
					bOK = ReadSeconds(value, what + ": crossfade", &target.nCrossfade) && bOK;
				}
				else
				{
					Error(LineOf(key), what + " has an unknown key " + Quote(name) + "; a target may have play or music, and fade_out, fade_in or crossfade");
					bOK = false;
				}
			}

			if (!bHasPlay && !bHasMusic)
			{
				Error(target.nLine, what + " needs play or music, such as { music = \"boss\" }");
				return false;
			}
			if (bHasCrossfade && (bHasFadeOut || bHasFadeIn))
			{
				Error(target.nLine, what + " has crossfade together with fade_out or fade_in; crossfade already fades both, so use either crossfade or fade_out and fade_in");
				return false;
			}

			return bOK;
		}

		void ReadTransitions(const toml::node &node, const std::string &what, std::vector<PlaylistTransition> &transitions)
		{
			const toml::table *lpTable = node.as_table();
			if (!lpTable)
			{
				WrongType(what, node, "a table of file patterns and targets");
				return;
			}

			// Patterns that are the same once normalized are the same key, as far as matching goes
			std::map<std::string, unsigned> seen;
			for (auto &&[key, value] : *lpTable)
			{
				PlaylistTransition transition;
				transition.written = Str(key);
				transition.nLine = LineOf(key);
				m_nTransitions++;

				if (!CheckPath(transition.written, transition.nLine, "file pattern", false))
					continue;

				transition.pattern = NormalizePlaylistName(transition.written);
				std::map<std::string, unsigned>::iterator itSeen = seen.find(transition.pattern);
				if (itSeen != seen.end())
				{
					Error(transition.nLine, what + ": " + QuotePlaylistPath(transition.written) + " is the same file pattern as on line " + std::to_string(itSeen->second) + " (case and / or \\ don't matter)");
					continue;
				}
				seen[transition.pattern] = transition.nLine;

				if (ReadTarget(value, what + ": " + QuotePlaylistPath(transition.written), transition.target))
					transitions.push_back(transition);
			}
		}

		void ReadTrack(const toml::node &node, const std::string &what, Playlist &playlist)
		{
			PlaylistTrack track;
			track.nLine = LineOf(node);
			m_nTracks++;

			const toml::node *lpFile = &node;
			if (const toml::table *lpTable = node.as_table())
			{
				lpFile = NULL;
				for (auto &&[key, value] : *lpTable)
				{
					std::string name = Str(key);
					if (name == "file")
						lpFile = &value;
					else if (name == "times")
					{
						const toml::value<int64_t> *lpTimes = value.as_integer();
						if (!lpTimes)
							WrongType(what + ": times", value, "a whole number");
						else if (lpTimes->get() < 1 || lpTimes->get() > PLAYLIST_MAX_TIMES)
							Error(LineOf(value), what + ": times should be from 1 to " + std::to_string(PLAYLIST_MAX_TIMES) + "; it is how many times the track plays in a row, not how many extra times");
						else
							track.nTimes = (unsigned)lpTimes->get();
					}
					else
						Error(LineOf(key), what + " has an unknown key " + Quote(name) + "; a track may have file and times");
				}

				if (!lpFile)
				{
					Error(track.nLine, what + " needs a file, as in { file = 'music\\t2.ogg', times = 2 }");
					return;
				}
			}

			const toml::value<std::string> *lpPath = lpFile->as_string();
			if (!lpPath)
			{
				WrongType(what, *lpFile, "a path, or a table such as { file = 'music\\t2.ogg', times = 2 }");
				return;
			}

			if (!CheckPath(lpPath->get(), LineOf(*lpFile), what, true))
				return;

			track.file = PlaylistPathForStorm(lpPath->get());
			playlist.tracks.push_back(track);
		}

		void ReadPlaylist(const std::string &name, unsigned nLine, const toml::node &node)
		{
			std::string what = "playlist " + Quote(name);

			if (name == "back" || name == "stop")
			{
				Error(nLine, Quote(name) + " can't be used as a playlist name, as it is a target of its own");
				return;
			}
			if (name.empty())
			{
				Error(nLine, "a playlist has an empty name");
				return;
			}

			const toml::table *lpTable = node.as_table();
			if (!lpTable)
			{
				WrongType(what, node, "a table, as in [playlist." + name + "]");
				return;
			}

			Playlist playlist;
			playlist.name = name;
			playlist.nLine = nLine;

			const toml::node *lpTimes = NULL, *lpTracks = NULL;
			for (auto &&[key, value] : *lpTable)
			{
				std::string keyName = Str(key);
				if (keyName == "tracks")
					lpTracks = &value;
				else if (keyName == "order")
				{
					const toml::value<std::string> *lpOrder = value.as_string();
					if (lpOrder && lpOrder->get() == "in order")
						playlist.bShuffle = false;
					else if (lpOrder && lpOrder->get() == "shuffle")
						playlist.bShuffle = true;
					else
						Error(LineOf(value), what + ": order should be \"in order\" or \"shuffle\"");
				}
				else if (keyName == "play")
				{
					const toml::value<std::string> *lpPlay = value.as_string();
					if (lpPlay && lpPlay->get() == "one")
						playlist.bPlayAll = false;
					else if (lpPlay && lpPlay->get() == "all")
						playlist.bPlayAll = true;
					else
						Error(LineOf(value), what + ": play should be \"one\" or \"all\"");
				}
				else if (keyName == "times")
					lpTimes = &value;
				else if (keyName == "crossfade")
					ReadSeconds(value, what + ": crossfade", &playlist.nCrossfade);
				else if (keyName == "next")
				{
					if (ReadTarget(value, what + ": next", playlist.next))
						playlist.bHasNext = true;
					m_nTransitions++;
				}
				else if (keyName == "on")
					ReadTransitions(value, what + ": on", playlist.on);
				else
					Error(LineOf(key), what + " has an unknown key " + Quote(keyName) + "; a playlist may have tracks, order, play, times, crossfade, next and on");
			}

			if (lpTimes)
			{
				if (const toml::value<int64_t> *lpInteger = lpTimes->as_integer())
				{
					if (lpInteger->get() < 1 || lpInteger->get() > PLAYLIST_MAX_TIMES)
						Error(LineOf(*lpTimes), what + ": times should be from 1 to " + std::to_string(PLAYLIST_MAX_TIMES) + ", or \"forever\"; it is the total number of plays, not the number of extra ones");
					else
						playlist.nTimes = (unsigned)lpInteger->get();
				}
				else if (lpTimes->is_string() && lpTimes->as_string()->get() == "forever")
					playlist.bForever = true;
				else
					WrongType(what + ": times", *lpTimes, "a whole number or \"forever\"");

				if (!playlist.bPlayAll)
					Error(LineOf(*lpTimes), what + ": times only has an effect with play = \"all\"; to play a track several times in a row, write { file = '...', times = 2 } in tracks");
			}

			if (!lpTracks)
				Error(nLine, what + " has no tracks");
			else if (const toml::array *lpArray = lpTracks->as_array())
			{
				if (lpArray->empty())
					Error(LineOf(*lpTracks), what + " has no tracks");

				size_t iTrack = 0;
				for (const toml::node &track : *lpArray)
					ReadTrack(track, what + ": track " + std::to_string(++iTrack), playlist);
			}
			else
				WrongType(what + ": tracks", *lpTracks, "a list of paths, such as ['music\\t1.ogg', 'music\\t2.ogg']");

			m_lpFile->playlists[name] = std::move(playlist);
		}

		bool ReadBoolean(const toml::node &node, const std::string &what, bool *lpbValue)
		{
			const toml::value<bool> *lpBoolean = node.as_boolean();
			if (!lpBoolean)
			{
				WrongType(what, node, "true or false");
				return false;
			}

			*lpbValue = lpBoolean->get();
			return true;
		}

		void ReadRoot(const toml::table &root)
		{
			bool bHasVersion = false;
			for (auto &&[key, value] : root)
			{
				std::string name = Str(key);
				if (name == "version")
				{
					bHasVersion = true;
					const toml::value<int64_t> *lpVersion = value.as_integer();
					if (!lpVersion)
						WrongType("version", value, "1");
					else if (lpVersion->get() != 1)
						Error(LineOf(value), "version " + std::to_string(lpVersion->get()) + " isn't supported; this ThunderGraft reads version 1");
				}
				else if (name == "music")
				{
					const toml::array *lpArray = value.as_array();
					if (!lpArray)
					{
						WrongType("music", value, "a list of file patterns, such as ['music\\*']");
						continue;
					}

					m_lpFile->bHasMusic = true;
					for (const toml::node &pattern : *lpArray)
					{
						const toml::value<std::string> *lpPattern = pattern.as_string();
						if (!lpPattern)
							WrongType("music", pattern, "a list of file patterns");
						else if (CheckPath(lpPattern->get(), LineOf(pattern), "music pattern", false))
							m_lpFile->music.push_back(NormalizePlaylistName(lpPattern->get()));
					}
				}
				else if (name == "on")
					ReadTransitions(value, "[on]", m_lpFile->on);
				else if (name == "playlist")
				{
					const toml::table *lpPlaylists = value.as_table();
					if (!lpPlaylists)
					{
						WrongType("playlist", value, "a table of playlists, as in [playlist.titlelist]");
						continue;
					}

					for (auto &&[playlistKey, playlistValue] : *lpPlaylists)
						ReadPlaylist(Str(playlistKey), LineOf(playlistKey), playlistValue);
				}
				else if (name == "map_playlists")
				{
					if (m_options.bMap)
						Error(LineOf(key), "map_playlists can only be set in the base file, not in a map's");
					else
						ReadBoolean(value, "map_playlists", &m_lpFile->bMapPlaylists);
				}
				else if (name == "inherit")
				{
					if (!m_options.bMap)
						Error(LineOf(key), "inherit can only be set in a map's playlist file, not in the base file");
					else
						ReadBoolean(value, "inherit", &m_lpFile->bInherit);
				}
				else
					Error(LineOf(key), "unknown key " + Quote(name) + "; the top level may have version, music, on, playlist" + (m_options.bMap ? ", inherit" : ", map_playlists"));
			}

			if (!bHasVersion)
				Error(0, "version = 1 is missing; it has to be at the top of the file");
		}

		void CheckReferences()
		{
			// toml++ keeps keys sorted, so the references weren't read in the order of the file; use each name's first line
			std::map<std::string, unsigned> unknown;
			for (const std::pair<std::string, unsigned> &reference : m_references)
			{
				if (m_lpFile->playlists.count(reference.first))
					continue;

				std::map<std::string, unsigned>::iterator itUnknown = unknown.find(reference.first);
				if (itUnknown == unknown.end())
					unknown[reference.first] = reference.second;
				else if (reference.second < itUnknown->second)
					itUnknown->second = reference.second;
			}

			for (const std::pair<const std::string, unsigned> &name : unknown)
			{
				// A map may use the base's playlists, which aren't known here
				if (m_options.bMap)
					m_lpFile->external.push_back(name);
				else
					Error(name.second, "there is no playlist named " + Quote(name.first));
			}
		}

		void CheckMapLimits()
		{
			if (!m_options.bMap)
				return;

			if (m_lpFile->playlists.size() > PLAYLIST_MAP_MAX_PLAYLISTS)
				Error(0, "a map's playlist file may have at most " + std::to_string(PLAYLIST_MAP_MAX_PLAYLISTS) + " playlists");
			if (m_nTracks > PLAYLIST_MAP_MAX_TRACKS)
				Error(0, "a map's playlist file may have at most " + std::to_string(PLAYLIST_MAP_MAX_TRACKS) + " tracks in all");
			if (m_nTransitions > PLAYLIST_MAP_MAX_TRANSITIONS)
				Error(0, "a map's playlist file may have at most " + std::to_string(PLAYLIST_MAP_MAX_TRANSITIONS) + " transitions in all");
			if (m_lpFile->music.size() > PLAYLIST_MAP_MAX_MUSIC_PATTERNS)
				Error(0, "a map's playlist file may have at most " + std::to_string(PLAYLIST_MAP_MAX_MUSIC_PATTERNS) + " music patterns");
		}

	public:
		CReader(const PlaylistReadOptions &options, PlaylistDiagnostics &diagnostics)
			: m_options(options), m_diagnostics(diagnostics)
		{
			m_bErrors = false;
			m_nTracks = 0;
			m_nTransitions = 0;
		}

		std::shared_ptr<const PlaylistFile> Read(const std::string &text)
		{
			if (m_options.bMap && text.size() > PLAYLIST_MAP_MAX_FILE_SIZE)
			{
				Error(0, "a map's playlist file may be at most " + std::to_string(PLAYLIST_MAP_MAX_FILE_SIZE / 1024) + " KB");
				return nullptr;
			}

			m_lpFile = std::make_shared<PlaylistFile>();
			m_lpFile->label = m_options.label;
			m_lpFile->bMap = m_options.bMap;
			m_lpFile->bFromDisk = m_options.bFromDisk;

			toml::parse_result result = toml::parse(std::string_view(text), std::string_view(m_options.label));
			if (!result)
			{
				std::string message(result.error().description());
				if (message.find("escape sequence") != std::string::npos)
					message += ". In double quotes, \\ starts an escape sequence; write paths in single quotes, as in 'music\\zerg1.wav', or with / instead of \\";
				Error(result.error().source().begin.line, message);
				return nullptr;
			}

			ReadRoot(result.table());
			CheckReferences();
			CheckMapLimits();

			if (m_bErrors)
				return nullptr;

			return m_lpFile;
		}
	};

	void FindAmbiguous(const std::vector<PlaylistTransition> &transitions, const std::string &label, const std::string &what, PlaylistDiagnostics &diagnostics)
	{
		for (size_t i = 0; i < transitions.size(); i++)
		{
			for (size_t j = i + 1; j < transitions.size(); j++)
			{
				const PlaylistTransition &a = transitions[i], &b = transitions[j];
				if (ComparePlaylistPatterns(a.pattern, b.pattern) != 0 || !PlaylistPatternsOverlap(a.pattern, b.pattern))
					continue;

				const PlaylistTransition &first = (a.pattern < b.pattern ? a : b), &second = (a.pattern < b.pattern ? b : a);
				diagnostics.Warning(label, second.nLine, what + ": " + QuotePlaylistPath(second.written) + " and " + QuotePlaylistPath(first.written) + " (line " + std::to_string(first.nLine) + ") are equally specific and can match the same file; for such a file, " + QuotePlaylistPath(first.written) + " wins, as it comes first alphabetically");
			}
		}
	}
}

std::string PlaylistTarget::Describe() const
{
	std::string name = (kind == PLAYLIST_TARGET_BACK ? "back" : kind == PLAYLIST_TARGET_STOP ? "stop" : playlist);
	if (!bMusic && !nFadeOut && !nFadeIn && !nCrossfade)
		return Quote(name);

	std::string text = std::string("{ ") + (bMusic ? "music" : "play") + " = " + Quote(name);
	if (nFadeOut)
		text += ", fade_out = " + FormatSeconds(nFadeOut);
	if (nFadeIn)
		text += ", fade_in = " + FormatSeconds(nFadeIn);
	if (nCrossfade)
		text += ", crossfade = " + FormatSeconds(nCrossfade);

	return text + " }";
}

const Playlist *PlaylistFile::Find(const std::string &name) const
{
	std::map<std::string, Playlist>::const_iterator itPlaylist = playlists.find(name);
	return itPlaylist != playlists.end() ? &itPlaylist->second : NULL;
}

// Not a static inside the function: making those thread-safe needs functions Windows XP lacks
static const std::vector<std::string> g_defaultMusic = { "music\\*", "data\\global\\music\\*" };

const std::vector<std::string> &PlaylistDefaultMusic()
{
	return g_defaultMusic;
}

std::shared_ptr<const PlaylistFile> ReadPlaylistFile(const std::string &text, const PlaylistReadOptions &options, PlaylistDiagnostics &diagnostics)
{
	try
	{
		CReader reader(options, diagnostics);
		return reader.Read(text);
	}
	catch (...)
	{
		// Out of memory; nothing else in here throws
		diagnostics.Error(options.label, 0, "not enough memory to read the file");
		return nullptr;
	}
}

void CheckPlaylistCombination(const PlaylistFile &map, const PlaylistFile *lpBase, PlaylistDiagnostics &diagnostics)
{
	for (const std::pair<std::string, unsigned> &name : map.external)
	{
		if (lpBase && map.bInherit && lpBase->Find(name.first))
			continue;

		diagnostics.Warning(map.label, name.second, "there is no playlist named " + Quote(name.first) + " in this file" + (map.bInherit ? " or in the base file" : "") + "; transitions to it are ignored while the map is loaded");
	}
}

void FindAmbiguousPlaylistPatterns(const PlaylistFile &file, PlaylistDiagnostics &diagnostics)
{
	FindAmbiguous(file.on, file.label, "[on]", diagnostics);
	for (const std::pair<const std::string, Playlist> &playlist : file.playlists)
		FindAmbiguous(playlist.second.on, file.label, "playlist " + Quote(playlist.first) + ": on", diagnostics);
}
