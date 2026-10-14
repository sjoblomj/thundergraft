/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

#include "PlaylistPlayer.h"
#include "DdaConversions.h"
#include "PlaylistPath.h"

#include <assert.h>
#include <stdio.h>
#include <algorithm>

namespace
{
	// Fading sounds are destroyed a little after their fade should be over, as the audio thread may be a little behind
	const uint64_t FADE_MARGIN_MS = 50;
	// How long before a track ends the next one is opened, so it can start without a gap
	const double PREPARE_SECONDS = 2.0;

	uint32_t Milliseconds(double nSeconds)
	{
		return nSeconds > 0 ? (uint32_t)(nSeconds * 1000 + 0.5) : 0;
	}

	// " from 1:05", where back resumes a sound
	std::string FromPosition(IAudioSound &sound, uint64_t nFrame)
	{
		if (!sound.GetSampleRate())
			return "";

		uint64_t nSeconds = nFrame / sound.GetSampleRate();
		char szPosition[32];
		snprintf(szPosition, sizeof(szPosition), " from %u:%02u", (unsigned)(nSeconds / 60), (unsigned)(nSeconds % 60));
		return szPosition;
	}
}

CPlaylistPlayer::CPlaylistPlayer()
{
	m_lpEngine = NULL;
	m_lpFiles = NULL;
	m_lpLog = NULL;
	m_nNow = 0;
	m_bHasMusicStream = false;
	m_musicStream = 0;
	m_nBackGeneration = 0;
	m_nVisitSerial = 0;
}

CPlaylistPlayer::~CPlaylistPlayer()
{
	EndAll();
}

void CPlaylistPlayer::Initialize(IAudioEngine *lpEngine, IPlaylistFiles *lpFiles, IPlaylistLog *lpLog, uint64_t nSeed)
{
	assert(lpEngine && lpFiles);

	m_lpEngine = lpEngine;
	m_lpFiles = lpFiles;
	m_lpLog = lpLog;
	m_random.Seed(nSeed);
}

bool CPlaylistPlayer::Tracing() const
{
	return m_lpLog && m_lpLog->TraceEnabled();
}

void CPlaylistPlayer::Trace(const std::string &text)
{
	if (Tracing())
		m_lpLog->Trace(text);
}

std::string CPlaylistPlayer::StreamName(const Stream &stream) const
{
	return m_lpLog ? m_lpLog->StreamName(stream.id) : "stream";
}

void CPlaylistPlayer::WarnTrack(const PlaylistRef &playlist, int nTrack, const std::string &problem)
{
	const PlaylistTrack &track = playlist.playlist->tracks[nTrack];
	if (!m_lpLog || !m_warnedTracks.insert(playlist.file->label + "\n" + track.file).second)
		return;

	m_lpLog->Warning(PlaylistDiagnostic{PLAYLIST_WARNING, playlist.file->label, track.nLine, "playlist \"" + playlist.playlist->name + "\": track " + QuotePlaylistPath(track.file) + " " + problem + ", so it is skipped"});
}

CPlaylistPlayer::Stream *CPlaylistPlayer::Find(PlaylistStreamId id) const
{
	std::map<PlaylistStreamId, std::unique_ptr<Stream>>::const_iterator itStream = m_streams.find(id);
	return itStream != m_streams.end() ? itStream->second.get() : NULL;
}

CPlaylistPlayer::Stream *CPlaylistPlayer::MusicStream() const
{
	return m_bHasMusicStream ? Find(m_musicStream) : NULL;
}

bool CPlaylistPlayer::IsMusicStream(const Stream &stream) const
{
	return m_bHasMusicStream && m_musicStream == stream.id;
}

void CPlaylistPlayer::ApplySettings(const Stream &stream, Slot &slot)
{
	if (!slot.lpSound)
		return;

	slot.lpSound->SetVolume(stream.settings.nVolume);
	slot.lpSound->SetPan(stream.settings.nPan);
	slot.lpSound->SetPitch(stream.settings.nPitch);
	slot.lpSound->SetMuted(stream.settings.bMuted);
}

void CPlaylistPlayer::StartSlot(Slot &slot)
{
	if (!slot.lpSound)
		return;

	if (slot.nFadeInOnStart > 0)
		slot.lpSound->Fade(0, 1, Milliseconds(slot.nFadeInOnStart));

	slot.lpSound->Start();
	slot.bStarted = true;
	slot.bDelayed = false;
	slot.nFadeInOnStart = 0;
}

void CPlaylistPlayer::FadeOut(Stream &stream, Slot &slot, double nSeconds)
{
	if (slot.lpSound && nSeconds > 0 && slot.bStarted && slot.lpSound->IsPlaying())
	{
		slot.lpSound->Fade(-1, 0, Milliseconds(nSeconds));
		stream.fading.push_back(Fading{std::move(slot), m_nNow + Milliseconds(nSeconds)});
	}

	slot = Slot();
}

void CPlaylistPlayer::DropNext(Stream &stream)
{
	stream.lpNext.reset();
}

// The game's file of a stream is read through the game's own handle, and two sounds can't read from one handle at once
void CPlaylistPlayer::DestroyGameFileSlots(Stream &stream)
{
	if (stream.primary.lpSound && stream.primary.nTrack < 0)
		stream.primary = Slot();
	if (stream.lpNext && stream.lpNext->slot.lpSound && stream.lpNext->slot.nTrack < 0)
		DropNext(stream);

	stream.fading.erase(std::remove_if(stream.fading.begin(), stream.fading.end(), [](const Fading &fading)
		{ return fading.slot.nTrack < 0; }), stream.fading.end());
}

bool CPlaylistPlayer::TimeLeft(const Stream &stream, const Slot &slot, uint64_t *lpnMilliseconds) const
{
	uint64_t nCursor, nLength;
	if (!slot.lpSound || !slot.lpSound->GetCursor(&nCursor) || !slot.lpSound->GetLength(&nLength) || !slot.lpSound->GetSampleRate())
		return false;

	double nPitch = stream.settings.nPitch > 0 ? stream.settings.nPitch : 1;
	uint64_t nFramesLeft = nLength > nCursor ? nLength - nCursor : 0;
	*lpnMilliseconds = (uint64_t)(nFramesLeft * 1000.0 / slot.lpSound->GetSampleRate() / nPitch);

	return true;
}

bool CPlaylistPlayer::OpenTrack(const Stream &stream, const PlaylistRef &playlist, int nTrack, bool bLoop, Slot &slot)
{
	const PlaylistTrack &track = playlist.playlist->tracks[nTrack];

	std::unique_ptr<IAudioFile> lpFile = m_lpFiles->OpenTrack(track.file);
	if (!lpFile)
	{
		WarnTrack(playlist, nTrack, "can't be found");
		return false;
	}

	std::unique_ptr<IAudioSound> lpSound = m_lpEngine->OpenSound(std::move(lpFile), bLoop);
	if (!lpSound)
	{
		WarnTrack(playlist, nTrack, "isn't in a format ThunderGraft can play");
		return false;
	}

	slot = Slot();
	slot.lpSound = std::move(lpSound);
	slot.nTrack = nTrack;
	slot.path = track.file;
	slot.bLoop = bLoop;
	slot.maps = m_layers.MapIds();
	ApplySettings(stream, slot);

	return true;
}

bool CPlaylistPlayer::OpenVisitTrack(const Stream &stream, const PlaylistRef &playlist, CPlaylistVisit &visit, Slot &slot)
{
	PlaylistPass &pass = m_layers.Pass(*playlist.playlist);

	// Missing tracks are skipped, but not forever: a playlist none of whose tracks can play ends the visit
	size_t nFailures = 0;
	for (int nTrack = visit.NextTrack(pass, m_random); nTrack >= 0; nTrack = visit.NextTrack(pass, m_random))
	{
		if (OpenTrack(stream, playlist, nTrack, visit.LoopsTrack(), slot))
			return true;

		if (++nFailures >= playlist.playlist->tracks.size())
			break;

		visit.SkipEntry(pass, m_random);
	}

	return false;
}

bool CPlaylistPlayer::StartVisit(const Stream &stream, const PlaylistRef &playlist, CPlaylistVisit &visit, Slot &slot)
{
	visit.Begin(*playlist.playlist, stream.bLoop, m_layers.Pass(*playlist.playlist), m_random);
	if (OpenVisitTrack(stream, playlist, visit, slot))
		return true;

	if (m_lpLog && m_warnedTracks.insert(playlist.file->label + "\nplaylist\n" + playlist.playlist->name).second)
		m_lpLog->Warning(PlaylistDiagnostic{PLAYLIST_WARNING, playlist.file->label, playlist.playlist->nLine, "playlist \"" + playlist.playlist->name + "\": none of its tracks can be played, so it is left out"});

	return false;
}

bool CPlaylistPlayer::OpenGameFile(Stream &stream, Slot &slot)
{
	DestroyGameFileSlots(stream);

	std::unique_ptr<IAudioFile> lpFile = m_lpFiles->OpenGameFile(stream.id);
	if (!lpFile)
		return false;

	std::unique_ptr<IAudioSound> lpSound = m_lpEngine->OpenSound(std::move(lpFile), stream.bLoop);
	if (!lpSound)
		return false;

	slot = Slot();
	slot.lpSound = std::move(lpSound);
	slot.nTrack = -1;
	slot.path = stream.bHasName ? stream.name : "the game's file";
	slot.bLoop = stream.bLoop;
	ApplySettings(stream, slot);

	return true;
}

bool CPlaylistPlayer::OpenBack(Stream &stream, const BackPoint &back, Slot &slot)
{
	if (back.bGameFile)
	{
		if (!OpenGameFile(stream, slot))
			return false;
	}
	else if (!OpenTrack(stream, back.playlist, back.nTrack, back.bLoop, slot))
		return false;

	if (back.nFrame)
		slot.lpSound->SeekToFrame(back.nFrame);

	return true;
}

CPlaylistPlayer::BackPoint CPlaylistPlayer::Snapshot(const Stream &stream) const
{
	BackPoint back;
	const Slot &slot = stream.primary;
	if (!slot.lpSound)
		return back;

	if (stream.state == STREAM_GAME)
	{
		if (!slot.lpSound->IsPlaying())
			return back;

		back.bGameFile = true;
		back.stream = stream.id;
	}
	else if (stream.state == STREAM_VISIT)
	{
		back.playlist = stream.playlist;
		back.visit = stream.visit;
		back.nTrack = slot.nTrack;
		back.bLoop = slot.bLoop;
	}
	else
		return back;

	uint64_t nFrame = 0;
	if (slot.bStarted)
		slot.lpSound->GetCursor(&nFrame);

	back.nFrame = nFrame;
	back.bValid = true;
	return back;
}

// Replaces what plays in a stream with slot: after the old fades out, while it fades out, or straight away
void CPlaylistPlayer::SetContent(Stream &stream, Slot &slot, double nFadeOut, double nFadeIn, double nCrossfade)
{
	DropNext(stream);

	Slot old = std::move(stream.primary);
	stream.primary = Slot();
	bool bOldPlaying = old.lpSound && old.bStarted && old.lpSound->IsPlaying();

	if (nCrossfade > 0)
	{
		FadeOut(stream, old, nCrossfade);
		slot.nFadeInOnStart = nCrossfade;
		StartSlot(slot);
	}
	else if (nFadeOut > 0 && bOldPlaying)
	{
		FadeOut(stream, old, nFadeOut);
		slot.nFadeInOnStart = nFadeIn;
		slot.bDelayed = true;
		slot.nStartAt = m_nNow + Milliseconds(nFadeOut);
	}
	else
	{
		old = Slot();
		slot.nFadeInOnStart = nFadeIn;
		StartSlot(slot);
	}

	stream.primary = std::move(slot);
}

void CPlaylistPlayer::SetVisit(Stream &stream, const PlaylistRef &playlist, const CPlaylistVisit &visit)
{
	stream.state = STREAM_VISIT;
	stream.playlist = playlist;
	stream.visit = visit;
	stream.nVisitSerial = ++m_nVisitSerial;
}

void CPlaylistPlayer::EndStream(Stream &stream, double nFadeOut)
{
	DropNext(stream);
	FadeOut(stream, stream.primary, nFadeOut);
	stream.primary = Slot();
	stream.playlist.Reset();
	stream.state = stream.fading.empty() ? STREAM_ENDED : STREAM_STOPPING;
}

void CPlaylistPlayer::SetMusicStream(Stream &stream)
{
	m_bHasMusicStream = true;
	m_musicStream = stream.id;
}

bool CPlaylistPlayer::PlayGameFile(Stream &stream, uint64_t nStartBytes)
{
	Slot slot;
	if (!OpenGameFile(stream, slot))
		return false;

	if (nStartBytes)
		slot.lpSound->SeekToFrame(DdaBytesToFrames(nStartBytes, slot.lpSound->GetChannels()));

	if (!slot.lpSound->Start())
		return false;
	slot.bStarted = true;

	DropNext(stream);
	stream.primary = std::move(slot);
	stream.state = STREAM_GAME;
	stream.playlist.Reset();

	return true;
}

// Called when a new stream becomes the music: remembers what the old music stream played, for back, and fades it out if the transition says so. A new stream waiting for that fade-out is the caller's business.
void CPlaylistPlayer::InterruptMusic(Stream &newStream, const PlaylistTarget &target, bool bSnapshot)
{
	Stream *lpOld = MusicStream();
	if (!lpOld || lpOld == &newStream)
		return;

	// Only something that was playing is interrupted; otherwise the back point stays as it was
	if (bSnapshot)
	{
		BackPoint back = Snapshot(*lpOld);
		if (back.bValid)
		{
			m_back = back;
			m_nBackGeneration++;
		}
	}

	double nFade = target.nCrossfade > 0 ? target.nCrossfade : target.nFadeOut;
	bool bOldPlaying = lpOld->primary.lpSound && lpOld->primary.lpSound->IsPlaying();
	if (nFade > 0 && bOldPlaying && (lpOld->state == STREAM_GAME || lpOld->state == STREAM_VISIT))
		EndStream(*lpOld, nFade);
}

bool CPlaylistPlayer::ApplyStartTransition(Stream &stream, const PlaylistMatch &match)
{
	const PlaylistTarget &target = match.transition->target;
	bool bMusic = stream.bMusicFile;

	// A cue: the stream plays as the game asked, and the music changes
	if (target.bMusic && !bMusic)
	{
		ApplyToMusic(target, true, "  ");
		return false;
	}

	Stream *lpOldMusic = MusicStream();
	bool bOldMusicPlaying = lpOldMusic && lpOldMusic != &stream && lpOldMusic->primary.lpSound && lpOldMusic->primary.lpSound->IsPlaying();
	// With fade_out, the new music waits for the old to fade out
	double nDelay = (bMusic && bOldMusicPlaying && target.nCrossfade <= 0) ? target.nFadeOut : 0;
	double nFadeIn = target.nCrossfade > 0 ? target.nCrossfade : target.nFadeIn;

	switch (target.kind)
	{
		case PLAYLIST_TARGET_PLAYLIST:
		{
			PlaylistRef playlist = m_layers.Find(target.playlist);
			if (!playlist)
			{
				Trace("  there is no playlist named \"" + target.playlist + "\", so the transition is ignored");
				return false;
			}

			// Going to the current playlist doesn't restart it: if it's playing, it carries on, now in this stream
			if (bMusic && playlist == m_current && lpOldMusic && lpOldMusic != &stream && lpOldMusic->state == STREAM_VISIT && lpOldMusic->playlist == playlist)
			{
				Stream &old = *lpOldMusic;
				stream.primary = std::move(old.primary);
				old.primary = Slot();
				stream.lpNext = std::move(old.lpNext);
				stream.playlist = old.playlist;
				stream.visit = old.visit;
				stream.nVisitSerial = old.nVisitSerial;
				stream.state = STREAM_VISIT;
				ApplySettings(stream, stream.primary);
				if (stream.lpNext)
					ApplySettings(stream, stream.lpNext->slot);

				old.playlist.Reset();
				old.state = old.fading.empty() ? STREAM_ENDED : STREAM_STOPPING;
				SetMusicStream(stream);

				Trace("  " + playlist.Name() + " is already playing, and carries on in " + StreamName(stream));
				return true;
			}

			Slot slot;
			CPlaylistVisit visit;
			if (!StartVisit(stream, playlist, visit, slot))
				return false;

			if (bMusic)
				InterruptMusic(stream, target, true);

			SetVisit(stream, playlist, visit);
			DropNext(stream);
			slot.nFadeInOnStart = nFadeIn;
			if (nDelay > 0)
			{
				slot.bDelayed = true;
				slot.nStartAt = m_nNow + Milliseconds(nDelay);
			}
			else
				StartSlot(slot);
			stream.primary = std::move(slot);

			if (bMusic)
			{
				SetMusicStream(stream);
				m_current = playlist;
			}

			Trace("  " + StreamName(stream) + " plays " + QuotePlaylistPath(stream.primary.path) + " of " + playlist.Name());
			return true;
		}

		case PLAYLIST_TARGET_BACK:
		{
			// A new stream can't go back to the game's own file of another
			if (!bMusic || !m_back.bValid || m_back.bGameFile)
			{
				Trace("  there is nothing to go back to, so the transition is ignored");
				return false;
			}

			Slot slot;
			if (!OpenBack(stream, m_back, slot))
				return false;

			BackPoint back = m_back;
			m_back = BackPoint();
			m_nBackGeneration++;

			InterruptMusic(stream, target, false);
			SetVisit(stream, back.playlist, back.visit);
			slot.nFadeInOnStart = nFadeIn;
			if (nDelay > 0)
			{
				slot.bDelayed = true;
				slot.nStartAt = m_nNow + Milliseconds(nDelay);
			}
			else
				StartSlot(slot);
			stream.primary = std::move(slot);

			SetMusicStream(stream);
			m_current = back.playlist;

			Trace("  " + StreamName(stream) + " goes back to " + QuotePlaylistPath(stream.primary.path) + " of " + back.playlist.Name() + FromPosition(*stream.primary.lpSound, back.nFrame));
			return true;
		}

		case PLAYLIST_TARGET_STOP:
		{
			if (bMusic)
			{
				InterruptMusic(stream, target, true);
				SetMusicStream(stream);
				m_current.Reset();
			}

			stream.state = STREAM_ENDED;
			Trace("  " + StreamName(stream) + " is silenced, and the game is told it ended");
			return true;
		}
	}

	return false;
}

void CPlaylistPlayer::ApplyToMusic(const PlaylistTarget &target, bool bInterrupt, const std::string &indent)
{
	Stream *lpMusic = MusicStream();
	bool bPlaying = lpMusic && (lpMusic->state == STREAM_VISIT || (lpMusic->state == STREAM_GAME && lpMusic->primary.lpSound && lpMusic->primary.lpSound->IsPlaying()));
	if (!bPlaying)
	{
		Trace(indent + "no music is playing, so the music isn't changed");
		return;
	}

	Stream &music = *lpMusic;
	switch (target.kind)
	{
		case PLAYLIST_TARGET_PLAYLIST:
		{
			PlaylistRef playlist = m_layers.Find(target.playlist);
			if (!playlist)
			{
				Trace(indent + "there is no playlist named \"" + target.playlist + "\", so the music isn't changed");
				return;
			}
			if (playlist == m_current && music.state == STREAM_VISIT && music.playlist == playlist)
			{
				Trace(indent + playlist.Name() + " is already playing in the music, and carries on");
				return;
			}

			Slot slot;
			CPlaylistVisit visit;
			if (!StartVisit(music, playlist, visit, slot))
				return;

			if (bInterrupt)
			{
				BackPoint back = Snapshot(music);
				if (back.bValid)
				{
					m_back = back;
					m_nBackGeneration++;
				}
			}

			SetContent(music, slot, target.nFadeOut, target.nFadeIn, target.nCrossfade);
			SetVisit(music, playlist, visit);
			m_current = playlist;

			Trace(indent + "the music (" + StreamName(music) + ") plays " + QuotePlaylistPath(music.primary.path) + " of " + playlist.Name());
			return;
		}

		case PLAYLIST_TARGET_BACK:
		{
			if (!m_back.bValid || (m_back.bGameFile && m_back.stream != music.id))
			{
				Trace(indent + "there is nothing to go back to, so the music isn't changed");
				return;
			}

			Slot slot;
			if (!OpenBack(music, m_back, slot))
			{
				Trace(indent + "what to go back to can't be played any more, so the music isn't changed");
				return;
			}

			BackPoint back = m_back;
			m_back = BackPoint();
			m_nBackGeneration++;

			SetContent(music, slot, target.nFadeOut, target.nFadeIn, target.nCrossfade);
			if (back.bGameFile)
			{
				music.state = STREAM_GAME;
				music.playlist.Reset();
				m_current.Reset();
			}
			else
			{
				SetVisit(music, back.playlist, back.visit);
				m_current = back.playlist;
			}

			Trace(indent + "the music (" + StreamName(music) + ") goes back to " + QuotePlaylistPath(music.primary.path) + (back.bGameFile ? "" : " of " + back.playlist.Name()) + FromPosition(*music.primary.lpSound, back.nFrame));
			return;
		}

		case PLAYLIST_TARGET_STOP:
		{
			if (bInterrupt)
			{
				BackPoint back = Snapshot(music);
				if (back.bValid)
				{
					m_back = back;
					m_nBackGeneration++;
				}
			}

			EndStream(music, target.nCrossfade > 0 ? target.nCrossfade : target.nFadeOut);
			m_current.Reset();

			Trace(indent + "the music (" + StreamName(music) + ") is silenced, and the game is told it ended");
			return;
		}
	}
}

// After a map is unloaded, the music stream plays as if it had just started under the remaining playlist files
void CPlaylistPlayer::Reevaluate(Stream &stream)
{
	PlaylistMatch match;
	if (stream.bHasName && m_layers.FindTransition(stream.name, m_current, &match, m_lpLog))
	{
		const PlaylistTarget &target = match.transition->target;
		Trace("  " + StreamName(stream) + " starts again: " + match.Describe() + " -> " + target.Describe());

		if (target.kind == PLAYLIST_TARGET_PLAYLIST)
		{
			PlaylistRef playlist = m_layers.Find(target.playlist);
			Slot slot;
			CPlaylistVisit visit;
			if (playlist && StartVisit(stream, playlist, visit, slot))
			{
				SetVisit(stream, playlist, visit);
				DropNext(stream);
				StartSlot(slot);
				stream.primary = std::move(slot);
				m_current = playlist;
				return;
			}
		}
		else if (target.kind == PLAYLIST_TARGET_STOP)
		{
			EndStream(stream, 0);
			m_current.Reset();
			return;
		}
	}

	m_current.Reset();
	if (!PlayGameFile(stream, 0))
		EndStream(stream, 0);
}

void CPlaylistPlayer::SetBase(std::shared_ptr<const PlaylistFile> base)
{
	m_layers.SetBase(base);
	m_current.Reset();
	m_back = BackPoint();
	m_nBackGeneration++;
}

bool CPlaylistPlayer::PushMap(uintptr_t tag, std::shared_ptr<const PlaylistFile> map)
{
	if (!m_layers.PushMap(tag, map))
		return false;

	PlaylistDiagnostics diagnostics;
	CheckPlaylistCombination(*map, map->bInherit ? m_layers.Base().get() : NULL, diagnostics);
	if (m_lpLog)
	{
		for (const PlaylistDiagnostic &diagnostic : diagnostics.All())
			m_lpLog->Warning(diagnostic);
	}

	Trace("map loaded: " + map->label);
	return true;
}

bool CPlaylistPlayer::PopMap(uintptr_t tag)
{
	CPlaylistLayers::Layer removed;
	if (!m_layers.RemoveMap(tag, &removed))
		return false;

	Trace("map unloaded: " + removed.file->label);

	auto openedOnMap = [&removed](const Slot &slot)
	{
		return slot.lpSound && std::find(slot.maps.begin(), slot.maps.end(), removed.nId) != slot.maps.end();
	};

	// Storm handles into the map's archive can't be trusted once it is closed, so every track opened while it was loaded stops now, with no time for a fade
	Stream *lpLostMusic = NULL;
	for (std::pair<const PlaylistStreamId, std::unique_ptr<Stream>> &entry : m_streams)
	{
		Stream &stream = *entry.second;

		if (stream.lpNext && (openedOnMap(stream.lpNext->slot) || stream.lpNext->playlist.file == removed.file || stream.lpNext->back.playlist.file == removed.file))
			DropNext(stream);

		stream.fading.erase(std::remove_if(stream.fading.begin(), stream.fading.end(), [&openedOnMap](const Fading &fading)
			{ return openedOnMap(fading.slot); }), stream.fading.end());
		if (stream.state == STREAM_STOPPING && stream.fading.empty())
			stream.state = STREAM_ENDED;

		if (openedOnMap(stream.primary) || (stream.state == STREAM_VISIT && stream.playlist.file == removed.file))
		{
			stream.primary = Slot();
			DropNext(stream);

			if (IsMusicStream(stream))
				lpLostMusic = &stream;
			else
			{
				EndStream(stream, 0);
				Trace("  " + StreamName(stream) + " played the map's music, and ends");
			}
		}
	}

	if (m_current.file == removed.file)
		m_current.Reset();
	if (m_back.bValid && m_back.playlist.file == removed.file)
	{
		m_back = BackPoint();
		m_nBackGeneration++;
	}

	if (lpLostMusic)
		Reevaluate(*lpLostMusic);

	return true;
}

bool CPlaylistPlayer::Begin(PlaylistStreamId id, const std::string *lpName, bool bLoop, uint64_t nStartBytes, const PlaylistStreamSettings &settings)
{
	End(id);

	std::unique_ptr<Stream> lpNewStream(new Stream);
	Stream &stream = *lpNewStream;
	stream.id = id;
	stream.bLoop = bLoop;
	stream.settings = settings;
	if (lpName)
	{
		stream.bHasName = true;
		stream.name = NormalizePlaylistName(*lpName);
		stream.bMusicFile = m_layers.IsMusic(stream.name);
	}
	m_streams[id] = std::move(lpNewStream);

	bool bHandled = false;
	if (!m_layers.Empty())
	{
		std::string started = "start " + (stream.bHasName ? QuotePlaylistPath(stream.name) : "of a file whose name isn't known") + " in " + StreamName(stream) + (stream.bMusicFile ? " (music)" : "") + (bLoop ? ", looping" : "");

		PlaylistMatch match;
		if (stream.bHasName && m_layers.FindTransition(stream.name, m_current, &match, m_lpLog))
		{
			Trace(started + ": " + match.Describe() + " -> " + match.transition->target.Describe());
			bHandled = ApplyStartTransition(stream, match);
		}
		else
			Trace(started + ": no transition, so the game's file plays");
	}

	if (!bHandled)
	{
		if (!PlayGameFile(stream, nStartBytes))
		{
			if (!m_layers.Empty())
				Trace("  the game's file can't be played");
			m_streams.erase(id);
			return false;
		}

		if (stream.bMusicFile)
		{
			SetMusicStream(stream);
			m_current.Reset();
		}
	}

	return true;
}

bool CPlaylistPlayer::End(PlaylistStreamId id)
{
	Stream *lpStream = Find(id);
	if (!lpStream)
		return false;

	if (IsMusicStream(*lpStream))
	{
		// The game stopping the music while a playlist plays stops the playlist. If the playlist's visit had already ended, the game is only moving on, and the playlist stays current for the transitions of the game's next music.
		if (lpStream->state == STREAM_VISIT)
			m_current.Reset();
		m_bHasMusicStream = false;
	}

	if (m_back.bValid && m_back.bGameFile && m_back.stream == id)
	{
		m_back = BackPoint();
		m_nBackGeneration++;
	}

	if (!m_layers.Empty())
		Trace("the game ends " + StreamName(*lpStream));

	m_streams.erase(id);
	return true;
}

void CPlaylistPlayer::EndAll()
{
	m_streams.clear();
	m_bHasMusicStream = false;
	m_current.Reset();
	m_back = BackPoint();
	m_nBackGeneration++;
}

void CPlaylistPlayer::Abandon()
{
	for (std::pair<const PlaylistStreamId, std::unique_ptr<Stream>> &entry : m_streams)
	{
		Stream &stream = *entry.second;
		stream.primary.lpSound.release();
		if (stream.lpNext)
			stream.lpNext->slot.lpSound.release();
		for (Fading &fading : stream.fading)
			fading.slot.lpSound.release();
	}

	m_streams.clear();
}

bool CPlaylistPlayer::IsPlaying(PlaylistStreamId id) const
{
	Stream *lpStream = Find(id);
	if (!lpStream)
		return false;

	switch (lpStream->state)
	{
		case STREAM_GAME:
			return lpStream->primary.lpSound && (lpStream->primary.bDelayed || lpStream->primary.lpSound->IsPlaying());
		case STREAM_VISIT:
		case STREAM_STOPPING:
			return true;
		default:
			return false;
	}
}

bool CPlaylistPlayer::GetPosition(PlaylistStreamId id, PlaylistStreamPosition *lpPosition) const
{
	Stream *lpStream = Find(id);
	if (!lpStream)
		return false;

	*lpPosition = PlaylistStreamPosition();

	const Slot *lpSlot = lpStream->primary.lpSound ? &lpStream->primary : (!lpStream->fading.empty() ? &lpStream->fading.back().slot : NULL);
	if (!lpSlot)
		return true;

	IAudioSound &sound = *lpSlot->lpSound;
	uint64_t nFrame = 0;
	if (lpSlot->bStarted && !sound.GetCursor(&nFrame))
		return false;

	lpPosition->nFrame = nFrame;
	lpPosition->bLengthKnown = sound.GetLength(&lpPosition->nLength);
	lpPosition->nChannels = sound.GetChannels();

	return true;
}

void CPlaylistPlayer::SetVolume(PlaylistStreamId id, float nVolume)
{
	Stream *lpStream = Find(id);
	if (!lpStream)
		return;

	lpStream->settings.nVolume = nVolume;
	if (lpStream->primary.lpSound)
		lpStream->primary.lpSound->SetVolume(nVolume);
	if (lpStream->lpNext && lpStream->lpNext->slot.lpSound)
		lpStream->lpNext->slot.lpSound->SetVolume(nVolume);
	for (Fading &fading : lpStream->fading)
		fading.slot.lpSound->SetVolume(nVolume);
}

void CPlaylistPlayer::SetPan(PlaylistStreamId id, float nPan)
{
	Stream *lpStream = Find(id);
	if (!lpStream)
		return;

	lpStream->settings.nPan = nPan;
	if (lpStream->primary.lpSound)
		lpStream->primary.lpSound->SetPan(nPan);
	if (lpStream->lpNext && lpStream->lpNext->slot.lpSound)
		lpStream->lpNext->slot.lpSound->SetPan(nPan);
	for (Fading &fading : lpStream->fading)
		fading.slot.lpSound->SetPan(nPan);
}

void CPlaylistPlayer::SetPitch(PlaylistStreamId id, float nRatio)
{
	Stream *lpStream = Find(id);
	if (!lpStream)
		return;

	lpStream->settings.nPitch = nRatio;
	if (lpStream->primary.lpSound)
		lpStream->primary.lpSound->SetPitch(nRatio);
	if (lpStream->lpNext && lpStream->lpNext->slot.lpSound)
		lpStream->lpNext->slot.lpSound->SetPitch(nRatio);
	for (Fading &fading : lpStream->fading)
		fading.slot.lpSound->SetPitch(nRatio);
}

void CPlaylistPlayer::SetMuted(PlaylistStreamId id, bool bMuted)
{
	Stream *lpStream = Find(id);
	if (!lpStream)
		return;

	lpStream->settings.bMuted = bMuted;
	if (lpStream->primary.lpSound)
		lpStream->primary.lpSound->SetMuted(bMuted);
	if (lpStream->lpNext && lpStream->lpNext->slot.lpSound)
		lpStream->lpNext->slot.lpSound->SetMuted(bMuted);
	for (Fading &fading : lpStream->fading)
		fading.slot.lpSound->SetMuted(bMuted);
}

double CPlaylistPlayer::PrepareWindow(const Stream &stream) const
{
	double nWindow = PREPARE_SECONDS;
	if (const Playlist *lpPlaylist = stream.playlist.playlist)
	{
		nWindow = std::max(nWindow, lpPlaylist->nCrossfade);
		if (lpPlaylist->bHasNext)
			nWindow = std::max(nWindow, std::max(lpPlaylist->next.nCrossfade, lpPlaylist->next.nFadeOut));
	}

	return nWindow + 0.5;
}

std::unique_ptr<CPlaylistPlayer::Step> CPlaylistPlayer::WorkOutNext(Stream &stream)
{
	std::unique_ptr<Step> lpStep(new Step);
	Step &step = *lpStep;

	step.visit = stream.visit;
	if (OpenVisitTrack(stream, stream.playlist, step.visit, step.slot))
	{
		step.kind = Step::STEP_TRACK;
		step.playlist = stream.playlist;
		step.nCrossfade = stream.playlist.playlist->nCrossfade;
	}
	else if (!stream.playlist.playlist->bHasNext)
		step.kind = Step::STEP_END;
	else
	{
		const PlaylistTarget &target = stream.playlist.playlist->next;
		step.target = target;
		step.kind = Step::STEP_END;

		if (target.bMusic && !IsMusicStream(stream))
			step.kind = Step::STEP_MUSIC;
		else if (target.kind == PLAYLIST_TARGET_PLAYLIST)
		{
			PlaylistRef playlist = m_layers.Find(target.playlist);
			if (playlist && StartVisit(stream, playlist, step.visit, step.slot))
			{
				step.kind = Step::STEP_VISIT;
				step.playlist = playlist;
			}
		}
		else if (target.kind == PLAYLIST_TARGET_BACK)
		{
			if (IsMusicStream(stream) && m_back.bValid && (!m_back.bGameFile || m_back.stream == stream.id) && OpenBack(stream, m_back, step.slot))
			{
				step.kind = Step::STEP_BACK;
				step.back = m_back;
				step.nBackGeneration = m_nBackGeneration;
			}
		}
		else
			step.kind = Step::STEP_STOP;

		step.nCrossfade = target.nCrossfade;
		step.nFadeIn = target.nFadeIn;
		step.nFadeOut = target.nFadeOut;
	}

	// Without a crossfade, the next track starts the moment this one ends
	if (step.slot.lpSound && step.nCrossfade <= 0)
	{
		if (step.nFadeIn > 0)
			step.slot.lpSound->Fade(0, 1, Milliseconds(step.nFadeIn));

		if (stream.primary.lpSound && stream.primary.lpSound->IsPlaying() && step.slot.lpSound->StartAfter(*stream.primary.lpSound))
		{
			step.bScheduled = true;
			step.slot.bStarted = true;
		}
	}

	return lpStep;
}

void CPlaylistPlayer::TakeNext(Stream &stream, double nCrossfade)
{
	std::unique_ptr<Step> lpStep = std::move(stream.lpNext);
	if (!lpStep)
		return;

	Step &step = *lpStep;
	std::string ending = "  " + StreamName(stream) + ": the visit of " + stream.playlist.Name() + " ends";

	// The back point changed since this step was worked out, or the stream is no longer the music
	if (step.kind == Step::STEP_BACK && (!IsMusicStream(stream) || step.nBackGeneration != m_nBackGeneration))
	{
		step.kind = Step::STEP_END;
		step.slot = Slot();
	}

	switch (step.kind)
	{
		case Step::STEP_TRACK:
		case Step::STEP_VISIT:
		case Step::STEP_BACK:
		{
			Slot old = std::move(stream.primary);
			stream.primary = Slot();
			if (nCrossfade > 0 && old.lpSound && old.lpSound->IsPlaying())
			{
				FadeOut(stream, old, nCrossfade);
				step.slot.nFadeInOnStart = nCrossfade;
				StartSlot(step.slot);
			}
			else
			{
				old = Slot();
				if (!step.slot.bStarted)
				{
					step.slot.nFadeInOnStart = step.nFadeIn;
					StartSlot(step.slot);
				}
			}
			stream.primary = std::move(step.slot);

			if (step.kind == Step::STEP_TRACK)
			{
				stream.visit = step.visit;
				Trace("  " + StreamName(stream) + " plays " + QuotePlaylistPath(stream.primary.path) + " of " + stream.playlist.Name());
			}
			else if (step.kind == Step::STEP_VISIT)
			{
				Trace(ending + "; next: " + step.target.Describe());
				SetVisit(stream, step.playlist, step.visit);
				if (IsMusicStream(stream))
					m_current = step.playlist;
				Trace("  " + StreamName(stream) + " plays " + QuotePlaylistPath(stream.primary.path) + " of " + step.playlist.Name());
			}
			else
			{
				Trace(ending + "; next: " + step.target.Describe());
				m_back = BackPoint();
				m_nBackGeneration++;
				if (step.back.bGameFile)
				{
					stream.state = STREAM_GAME;
					stream.playlist.Reset();
					m_current.Reset();
				}
				else
				{
					SetVisit(stream, step.back.playlist, step.back.visit);
					m_current = step.back.playlist;
				}
				Trace("  " + StreamName(stream) + " goes back to " + QuotePlaylistPath(stream.primary.path) + (step.back.bGameFile ? "" : " of " + step.back.playlist.Name()) + FromPosition(*stream.primary.lpSound, step.back.nFrame));
			}
			return;
		}

		case Step::STEP_END:
			Trace(ending + (stream.playlist.playlist && stream.playlist.playlist->bHasNext ? "; its next " + stream.playlist.playlist->next.Describe() + " can't be followed" : "") + ", and the game is told the stream ended");
			EndStream(stream, 0);
			return;

		case Step::STEP_STOP:
			Trace(ending + "; next: \"stop\", so the game is told the stream ended");
			if (IsMusicStream(stream))
				m_current.Reset();
			EndStream(stream, 0);
			return;

		case Step::STEP_MUSIC:
			Trace(ending + ", and the game is told the stream ended; next: " + step.target.Describe());
			EndStream(stream, 0);
			ApplyToMusic(step.target, false, "  ");
			return;
	}
}

void CPlaylistPlayer::UpdateStream(Stream &stream)
{
	stream.fading.erase(std::remove_if(stream.fading.begin(), stream.fading.end(), [this](const Fading &fading)
		{ return m_nNow >= fading.nEndAt + FADE_MARGIN_MS || !fading.slot.lpSound->IsPlaying(); }), stream.fading.end());

	// A start that waited for the sound before it to fade out
	if (stream.primary.lpSound && stream.primary.bDelayed && m_nNow >= stream.primary.nStartAt)
		StartSlot(stream.primary);

	if (stream.state == STREAM_STOPPING && stream.fading.empty())
		stream.state = STREAM_ENDED;

	if (stream.state != STREAM_VISIT || !stream.primary.lpSound || !stream.primary.bStarted)
		return;

	if (!stream.primary.lpSound->IsPlaying())
	{
		if (!stream.lpNext)
			stream.lpNext = WorkOutNext(stream);
		TakeNext(stream, 0);
		return;
	}

	uint64_t nLeft;
	if (stream.primary.bLoop || !TimeLeft(stream, stream.primary, &nLeft))
		return;

	if (!stream.lpNext && nLeft <= Milliseconds(PrepareWindow(stream)))
		stream.lpNext = WorkOutNext(stream);
	if (!stream.lpNext)
		return;

	Step &step = *stream.lpNext;
	if (step.slot.lpSound && step.nCrossfade > 0 && nLeft <= Milliseconds(step.nCrossfade))
	{
		TakeNext(stream, nLeft / 1000.0);
		return;
	}

	// fade_out in next fades out the end of the visit's last track
	if (step.nFadeOut > 0 && !step.bFadingOut && nLeft <= Milliseconds(step.nFadeOut))
	{
		stream.primary.lpSound->Fade(-1, 0, (uint32_t)nLeft);
		step.bFadingOut = true;
	}
}

void CPlaylistPlayer::Update(uint64_t nNowMs)
{
	m_nNow = nNowMs;

	for (std::pair<const PlaylistStreamId, std::unique_ptr<Stream>> &entry : m_streams)
		UpdateStream(*entry.second);
}

bool CPlaylistPlayer::GetMusicStream(PlaylistStreamId *lpId) const
{
	if (!MusicStream())
		return false;

	*lpId = m_musicStream;
	return true;
}

IAudioSound *CPlaylistPlayer::CurrentSound(PlaylistStreamId id) const
{
	Stream *lpStream = Find(id);
	if (!lpStream)
		return NULL;

	if (lpStream->primary.lpSound)
		return lpStream->primary.lpSound.get();

	return lpStream->fading.empty() ? NULL : lpStream->fading.back().slot.lpSound.get();
}

uint64_t CPlaylistPlayer::WaitingFor(PlaylistStreamId id) const
{
	Stream *lpStream = Find(id);
	if (!lpStream || !lpStream->primary.lpSound || !lpStream->primary.bDelayed || lpStream->primary.nStartAt <= m_nNow)
		return 0;

	return lpStream->primary.nStartAt - m_nNow;
}

unsigned CPlaylistPlayer::VisitSerial(PlaylistStreamId id) const
{
	Stream *lpStream = Find(id);
	return lpStream ? lpStream->nVisitSerial : 0;
}

PlaylistRef CPlaylistPlayer::StreamPlaylist(PlaylistStreamId id) const
{
	Stream *lpStream = Find(id);
	return (lpStream && lpStream->state == STREAM_VISIT) ? lpStream->playlist : PlaylistRef();
}

std::string CPlaylistPlayer::DescribeStream(PlaylistStreamId id) const
{
	Stream *lpStream = Find(id);
	if (!lpStream)
		return "not playing";

	switch (lpStream->state)
	{
		case STREAM_GAME:
			return "the game's file";
		case STREAM_VISIT:
			return QuotePlaylistPath(lpStream->primary.path) + " of " + lpStream->playlist.Name() + (lpStream->primary.bDelayed ? ", once the music before it has faded out" : "");
		case STREAM_STOPPING:
			return "fading out";
		default:
			return "ended";
	}
}
