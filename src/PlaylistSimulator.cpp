/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

#include "PlaylistSimulator.h"
#include "PlaylistPath.h"
#include "PlaylistPlayer.h"

#include <math.h>
#include <stdlib.h>
#include <map>
#include <set>
#include <vector>

namespace
{
	const unsigned SIM_SAMPLE_RATE = 44100;
	const double SIM_DEFAULT_LENGTH = 120;
	// Time passes in steps this long, about as often as the plugin's update thread runs
	const uint64_t SIM_STEP_MS = 50;

	// A file that is only a name and a length
	class CSimFile : public IAudioFile
	{
	public:
		std::string path;
		uint64_t nFrames;

		CSimFile(const std::string &filePath, uint64_t nLength) : path(filePath), nFrames(nLength) {}

		virtual bool Read(void *, size_t, size_t *lpnBytesRead)
		{
			*lpnBytesRead = 0;
			return true;
		}

		virtual bool Seek(int64_t, AudioSeekOrigin)
		{ return true; }

		virtual bool Tell(int64_t *lpnPosition)
		{
			*lpnPosition = 0;
			return true;
		}

		virtual bool GetSize(uint64_t *lpnSize)
		{
			*lpnSize = 0;
			return true;
		}
	};

	class CSimEngine;

	class CSimSound : public IAudioSound
	{
	public:
		CSimEngine &engine;
		std::string path;
		uint64_t nLength;
		bool bLoop;
		uint64_t nCursor = 0;
		bool bStarted = false;
		bool bAtEnd = false;
		// Starts when this one ends
		CSimSound *lpAfter = NULL;

		CSimSound(CSimEngine &simEngine, const std::string &filePath, uint64_t nFrames, bool bLooping);
		virtual ~CSimSound();

		virtual unsigned GetChannels() const { return 2; }
		virtual unsigned GetSampleRate() const { return SIM_SAMPLE_RATE; }

		virtual bool SeekToFrame(uint64_t nFrame)
		{
			nCursor = nFrame < nLength ? nFrame : nLength;
			return true;
		}

		virtual bool Start()
		{
			bStarted = true;
			return true;
		}

		virtual bool StartAfter(IAudioSound &previous)
		{
			CSimSound *lpPrevious = dynamic_cast<CSimSound *>(&previous);
			if (!lpPrevious || lpPrevious->bLoop)
				return false;

			lpAfter = lpPrevious;
			return true;
		}

		virtual void Fade(float, float, uint32_t) {}
		virtual void SetVolume(float) {}
		virtual void SetPan(float) {}
		virtual void SetPitch(float) {}
		virtual void SetMuted(bool) {}

		virtual bool IsPlaying() { return !bAtEnd; }

		virtual bool GetCursor(uint64_t *lpnFrame)
		{
			*lpnFrame = nCursor;
			return true;
		}

		virtual bool GetLength(uint64_t *lpnFrames)
		{
			*lpnFrames = nLength;
			return true;
		}

		void Advance(uint64_t nFrames)
		{
			if (!bStarted || bAtEnd)
				return;

			nCursor += nFrames;
			if (bLoop && nLength)
				nCursor %= nLength;
			else if (nCursor >= nLength)
			{
				nCursor = nLength;
				bAtEnd = true;
			}
		}
	};

	class CSimEngine : public IAudioEngine
	{
	public:
		std::set<CSimSound *> sounds;

		virtual std::unique_ptr<IAudioSound> OpenSound(std::unique_ptr<IAudioFile> lpFile, bool bLoop)
		{
			CSimFile *lpSimFile = dynamic_cast<CSimFile *>(lpFile.get());
			if (!lpSimFile)
				return nullptr;

			return std::unique_ptr<IAudioSound>(new CSimSound(*this, lpSimFile->path, lpSimFile->nFrames, bLoop));
		}

		// Sounds scheduled to start when lpSound ends start now
		void StartFollowers(CSimSound *lpSound)
		{
			for (CSimSound *lpOther : sounds)
			{
				if (lpOther->lpAfter == lpSound)
				{
					lpOther->lpAfter = NULL;
					lpOther->bStarted = true;
				}
			}
		}

		void Advance(uint64_t nMilliseconds)
		{
			uint64_t nFrames = nMilliseconds * SIM_SAMPLE_RATE / 1000;
			std::vector<CSimSound *> ended;
			for (CSimSound *lpSound : sounds)
			{
				bool bWasPlaying = !lpSound->bAtEnd && lpSound->bStarted;
				lpSound->Advance(nFrames);
				if (bWasPlaying && lpSound->bAtEnd)
					ended.push_back(lpSound);
			}

			for (CSimSound *lpSound : ended)
				StartFollowers(lpSound);
		}

		// Plays the sound to its end at once. Returns false for a looping sound, which has no end.
		bool Finish(CSimSound *lpSound)
		{
			if (lpSound->bLoop)
				return false;

			lpSound->bStarted = true;
			lpSound->nCursor = lpSound->nLength;
			lpSound->bAtEnd = true;
			StartFollowers(lpSound);
			return true;
		}
	};

	CSimSound::CSimSound(CSimEngine &simEngine, const std::string &filePath, uint64_t nFrames, bool bLooping)
		: engine(simEngine), path(filePath), nLength(nFrames), bLoop(bLooping)
	{
		engine.sounds.insert(this);
	}

	CSimSound::~CSimSound()
	{
		engine.sounds.erase(this);
		for (CSimSound *lpOther : engine.sounds)
		{
			if (lpOther->lpAfter == this)
				lpOther->lpAfter = NULL;
		}
	}

	struct SimStream
	{
		std::string name;
		std::string file;
		bool bPlaying = true;
	};

	class CSimulator : public IPlaylistFiles, public IPlaylistLog
	{
	private:
		const PlaylistSimulation &m_simulation;
		std::string &m_output;

		CSimEngine m_engine;
		CPlaylistPlayer m_player;
		uint64_t m_nNow = 0;

		std::map<PlaylistStreamId, SimStream> m_streams;
		std::map<std::string, PlaylistStreamId> m_streamNames;
		PlaylistStreamId m_nNextStream = 1;
		PlaylistStreamId m_lastStarted = 0;

		std::map<std::string, double> m_lengths;
		std::set<std::string> m_missing;

		std::vector<std::pair<std::string, uintptr_t>> m_maps;
		uintptr_t m_nNextMap = 1;

		std::string m_location;
		bool m_bOK = true;

		uint64_t LengthOf(const std::string &path) const
		{
			std::map<std::string, double>::const_iterator itLength = m_lengths.find(NormalizePlaylistName(path));
			double nSeconds = itLength != m_lengths.end() ? itLength->second : SIM_DEFAULT_LENGTH;
			return (uint64_t)(nSeconds * SIM_SAMPLE_RATE);
		}

		void Line(const std::string &text)
		{
			m_output += text + "\n";
		}

		void Problem(const std::string &text)
		{
			Line(m_location + ": " + text);
			m_bOK = false;
		}

		static std::vector<std::string> Tokenize(const std::string &line, bool *lpbOK)
		{
			std::vector<std::string> tokens;
			*lpbOK = true;

			size_t iChar = 0;
			while (iChar < line.size())
			{
				char c = line[iChar];
				if (c == ' ' || c == '\t' || c == '\r')
					iChar++;
				else if (c == '#')
					break;
				else if (c == '\'' || c == '"')
				{
					size_t iEnd = line.find(c, iChar + 1);
					if (iEnd == std::string::npos)
					{
						*lpbOK = false;
						return tokens;
					}

					tokens.push_back(line.substr(iChar + 1, iEnd - iChar - 1));
					iChar = iEnd + 1;
				}
				else
				{
					size_t iEnd = line.find_first_of(" \t\r", iChar);
					if (iEnd == std::string::npos)
						iEnd = line.size();

					tokens.push_back(line.substr(iChar, iEnd - iChar));
					iChar = iEnd;
				}
			}

			return tokens;
		}

		static bool ParseSeconds(const std::string &text, double *lpnSeconds)
		{
			char *lpEnd = NULL;
			double nSeconds = strtod(text.c_str(), &lpEnd);
			if (text.empty() || *lpEnd || !(nSeconds >= 0) || nSeconds > 1e7)
				return false;

			*lpnSeconds = nSeconds;
			return true;
		}

		// The stream an event is about
		bool PickStream(const std::vector<std::string> &tokens, size_t iToken, PlaylistStreamId *lpId)
		{
			if (tokens.size() > iToken + 1)
			{
				Problem("too many words");
				return false;
			}

			if (tokens.size() == iToken + 1)
			{
				std::map<std::string, PlaylistStreamId>::iterator itName = m_streamNames.find(tokens[iToken]);
				if (itName == m_streamNames.end() || !m_player.HasStream(itName->second))
				{
					Problem("there is no stream called " + tokens[iToken]);
					return false;
				}

				*lpId = itName->second;
				return true;
			}

			if (m_player.GetMusicStream(lpId))
				return true;

			if (m_lastStarted && m_player.HasStream(m_lastStarted))
			{
				*lpId = m_lastStarted;
				return true;
			}

			Problem("no stream is playing");
			return false;
		}

		void ReportEnded()
		{
			for (std::pair<const PlaylistStreamId, SimStream> &entry : m_streams)
			{
				SimStream &stream = entry.second;
				bool bPlaying = m_player.IsPlaying(entry.first);
				if (stream.bPlaying && !bPlaying)
					Line("  " + stream.name + " has ended, as far as the game can tell");
				stream.bPlaying = bPlaying;
			}
		}

		void Update()
		{
			m_player.Update(m_nNow);
			ReportEnded();
		}

		void Status()
		{
			PlaylistRef current = m_player.CurrentPlaylist();
			std::string status = "= current playlist: " + current.Name();

			PlaylistStreamId music;
			if (m_player.GetMusicStream(&music))
				status += "; music: " + m_streams[music].name + ", " + m_player.DescribeStream(music);
			else
				status += "; no music stream";

			Line(status);
		}

		void Wait(uint64_t nMilliseconds)
		{
			uint64_t nEnd = m_nNow + nMilliseconds;
			while (m_nNow < nEnd)
			{
				uint64_t nStep = (nEnd - m_nNow < SIM_STEP_MS) ? nEnd - m_nNow : SIM_STEP_MS;
				m_engine.Advance(nStep);
				m_nNow += nStep;
				Update();
			}
		}

		bool FinishTrack(PlaylistStreamId id)
		{
			// A track waiting for the music before it to fade out has to start before it can end
			if (uint64_t nWaiting = m_player.WaitingFor(id))
			{
				Line("  (waiting " + std::to_string(nWaiting / 1000) + "." + std::to_string(nWaiting % 1000 / 100) + " seconds for the music before it to fade out)");
				Wait(nWaiting);
			}

			CSimSound *lpSound = dynamic_cast<CSimSound *>(m_player.CurrentSound(id));
			if (!lpSound)
				return false;

			if (!m_engine.Finish(lpSound))
			{
				Line("  the track loops, so it never ends");
				return false;
			}

			Update();
			return true;
		}

		void Start(const std::vector<std::string> &tokens)
		{
			if (tokens.size() < 2)
			{
				Problem("start needs a file");
				return;
			}

			bool bLoop = false;
			std::string name;
			for (size_t iToken = 2; iToken < tokens.size(); iToken++)
			{
				if (tokens[iToken] == "loop")
					bLoop = true;
				else if (tokens[iToken] == "as" && iToken + 1 < tokens.size())
					name = tokens[++iToken];
				else
				{
					Problem("unexpected " + tokens[iToken] + "; start takes a file, then loop and as <name>");
					return;
				}
			}

			PlaylistStreamId id = m_nNextStream++;
			if (name.empty())
				name = "s" + std::to_string(id);
			if (m_streamNames.count(name) && m_player.HasStream(m_streamNames[name]))
			{
				Problem("a stream called " + name + " is already playing");
				return;
			}

			m_streams[id] = SimStream{name, tokens[1], true};
			m_streamNames[name] = id;

			if (!m_player.Begin(id, &tokens[1], bLoop, 0, PlaylistStreamSettings()))
			{
				Line("  " + name + " can't be played");
				m_streams[id].bPlaying = false;
				return;
			}

			m_lastStarted = id;
			ReportEnded();
		}

		void EndVisit(PlaylistStreamId id)
		{
			unsigned nSerial = m_player.VisitSerial(id);
			PlaylistRef playlist = m_player.StreamPlaylist(id);
			if (!playlist)
			{
				Line("  " + m_streams[id].name + " isn't playing a playlist");
				return;
			}
			if (playlist.playlist->bForever)
			{
				Line("  the visit of " + playlist.Name() + " never ends, as it plays forever");
				return;
			}

			// A visit plays at most every entry, times times, but give up long before anything runs away
			for (unsigned nTracks = 0; nTracks < 10000; nTracks++)
			{
				if (!m_player.IsPlaying(id) || m_player.VisitSerial(id) != nSerial || !m_player.StreamPlaylist(id))
					return;

				IAudioSound *lpBefore = m_player.CurrentSound(id);
				if (!FinishTrack(id))
					return;
				if (m_player.CurrentSound(id) == lpBefore && m_player.IsPlaying(id) && m_player.VisitSerial(id) == nSerial)
				{
					Line("  the track doesn't end");
					return;
				}
			}
		}

		void LoadMap(const std::vector<std::string> &tokens)
		{
			if (tokens.size() != 2 && !(tokens.size() == 4 && tokens[2] == "as"))
			{
				Problem("load-map takes a playlist file, and optionally as <name>");
				return;
			}

			std::string text;
			if (!m_simulation.readFile || !m_simulation.readFile(tokens[1], &text))
			{
				Problem("can't read " + tokens[1]);
				return;
			}

			PlaylistReadOptions options;
			options.label = tokens[1];
			options.bMap = true;

			PlaylistDiagnostics diagnostics;
			std::shared_ptr<const PlaylistFile> map = ReadPlaylistFile(text, options, diagnostics);
			for (const PlaylistDiagnostic &diagnostic : diagnostics.All())
				Line("  " + FormatPlaylistDiagnostic(diagnostic));
			if (!map)
			{
				Line("  the map's playlist file has errors, so it isn't used");
				return;
			}

			uintptr_t tag = m_nNextMap++;
			std::string name = tokens.size() == 4 ? tokens[3] : tokens[1];
			if (!m_player.PushMap(tag, map))
			{
				Line("  the base file has map_playlists = false, so the map's playlist file isn't used");
				return;
			}

			m_maps.push_back(std::make_pair(name, tag));
		}

		void UnloadMap(const std::vector<std::string> &tokens)
		{
			if (tokens.size() > 2)
			{
				Problem("unload-map takes at most a name");
				return;
			}
			if (m_maps.empty())
			{
				Problem("no map is loaded");
				return;
			}

			size_t iMap = m_maps.size() - 1;
			if (tokens.size() == 2)
			{
				for (iMap = m_maps.size(); iMap-- > 0;)
				{
					if (m_maps[iMap].first == tokens[1])
						break;
				}

				if (iMap >= m_maps.size())
				{
					Problem("no map called " + tokens[1] + " is loaded");
					return;
				}
			}

			m_player.PopMap(m_maps[iMap].second);
			m_maps.erase(m_maps.begin() + iMap);
			Update();
		}

		void Run(const std::vector<std::string> &tokens)
		{
			const std::string &command = tokens[0];
			PlaylistStreamId id;
			double nSeconds;

			if (command == "start")
				Start(tokens);
			else if (command == "end-track")
			{
				if (PickStream(tokens, 1, &id) && !FinishTrack(id) && !m_player.CurrentSound(id))
					Line("  " + m_streams[id].name + " plays nothing");
			}
			else if (command == "end-visit")
			{
				if (PickStream(tokens, 1, &id))
					EndVisit(id);
			}
			else if (command == "stop")
			{
				if (PickStream(tokens, 1, &id))
				{
					m_player.End(id);
					m_streams[id].bPlaying = false;
				}
			}
			else if (command == "wait")
			{
				if (tokens.size() != 2 || !ParseSeconds(tokens[1], &nSeconds))
				{
					Problem("wait takes a number of seconds");
					return;
				}

				Wait((uint64_t)(nSeconds * 1000 + 0.5));
			}
			else if (command == "length")
			{
				if (tokens.size() != 3 || !ParseSeconds(tokens[2], &nSeconds))
				{
					Problem("length takes a file and a number of seconds");
					return;
				}

				m_lengths[NormalizePlaylistName(tokens[1])] = nSeconds;
			}
			else if (command == "missing")
			{
				if (tokens.size() != 2)
				{
					Problem("missing takes a file");
					return;
				}

				m_missing.insert(NormalizePlaylistName(tokens[1]));
			}
			else if (command == "load-map")
				LoadMap(tokens);
			else if (command == "unload-map")
				UnloadMap(tokens);
			else
				Problem("unknown event " + command + "; events are start, end-track, end-visit, stop, wait, length, missing, load-map and unload-map");
		}

	public:
		CSimulator(const PlaylistSimulation &simulation, std::string &output)
			: m_simulation(simulation), m_output(output)
		{
			m_player.Initialize(&m_engine, this, this, simulation.nSeed);
			m_player.SetBase(simulation.base);
		}

		~CSimulator()
		{
			// The sounds have to go before the engine they belong to
			m_player.EndAll();
		}

		bool Run()
		{
			const std::string &events = m_simulation.events;
			size_t iStart = 0;
			unsigned nLine = 0;
			while (iStart < events.size())
			{
				size_t iEnd = events.find('\n', iStart);
				if (iEnd == std::string::npos)
					iEnd = events.size();

				std::string line = events.substr(iStart, iEnd - iStart);
				iStart = iEnd + 1;
				nLine++;
				m_location = m_simulation.eventsLabel + ":" + std::to_string(nLine);

				bool bOK;
				std::vector<std::string> tokens = Tokenize(line, &bOK);
				if (!bOK)
				{
					Problem("a quote isn't closed");
					continue;
				}
				if (tokens.empty())
					continue;

				std::string shown;
				for (const std::string &token : tokens)
					shown += (shown.empty() ? "" : " ") + (token.find_first_of(" \t") != std::string::npos ? "'" + token + "'" : token);
				Line("> " + shown);

				Run(tokens);
				Status();
			}

			return m_bOK;
		}

		// IPlaylistFiles
		virtual std::unique_ptr<IAudioFile> OpenTrack(const std::string &path)
		{
			if (m_missing.count(NormalizePlaylistName(path)))
				return nullptr;

			return std::unique_ptr<IAudioFile>(new CSimFile(path, LengthOf(path)));
		}

		virtual std::unique_ptr<IAudioFile> OpenGameFile(PlaylistStreamId id)
		{
			const std::string &file = m_streams[id].file;
			if (m_missing.count(NormalizePlaylistName(file)))
				return nullptr;

			return std::unique_ptr<IAudioFile>(new CSimFile(file, LengthOf(file)));
		}

		// IPlaylistLog
		virtual void Warning(const PlaylistDiagnostic &diagnostic)
		{ Line("  " + FormatPlaylistDiagnostic(diagnostic)); }

		virtual bool TraceEnabled()
		{ return true; }

		virtual void Trace(const std::string &text)
		{ Line(text.compare(0, 2, "  ") == 0 ? text : "  " + text); }

		virtual std::string StreamName(PlaylistStreamId id)
		{
			std::map<PlaylistStreamId, SimStream>::iterator itStream = m_streams.find(id);
			return itStream != m_streams.end() ? itStream->second.name : "s" + std::to_string(id);
		}
	};
}

bool RunPlaylistSimulation(const PlaylistSimulation &simulation, std::string *lpOutput)
{
	lpOutput->clear();
	CSimulator simulator(simulation, *lpOutput);
	return simulator.Run();
}
