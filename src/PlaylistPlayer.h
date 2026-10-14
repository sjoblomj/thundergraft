/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	Plays the streams the game starts, replacing them with playlists where the playlist files say so. See docs/playlists-design.md for the model; in short:

	- A stream is what the game starts with SFileDdaBegin(Ex), identified by its file handle. When one starts, the transitions for its file name decide what plays in it: the game's own file, or a playlist. Playing a playlist in a stream is a visit, which ends after one entry or the whole list (see PlaylistVisit.h); then the playlist's next decides what follows, and without one the stream ends, as far as the game can tell.
	- Streams whose files are music (see the music key) carry the state: the current playlist, which is the only thing transitions depend on besides the file name. The most recently started music stream is the music stream. A { music = ... } target changes what plays in it, whichever stream the event was about.
	- A transition that changes the music because a stream started interrupts what was playing, and back returns to it, from where it was.
	- Tracks change without gaps, crossfade or fade where the playlist says so. Fades multiply with the stream's volume and muting.

	With no playlist file, every stream simply plays the game's file, as the DDA functions would.

	The player isn't thread-safe; CDdaPlayer calls it with its lock held. Time comes from the caller, in Update, so tests and the simulator can control it.

	Platform-independent, so it can be tested anywhere.
*/

#ifndef PLAYLISTPLAYER_H
#define PLAYLISTPLAYER_H

#include <stdint.h>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "AudioEngine.h"
#include "PlaylistLayers.h"

// How the player gets at files
class IPlaylistFiles
{
public:
	virtual ~IPlaylistFiles() {}

	// Opens a track a playlist names, through Storm, as the game opens its own files. NULL if it can't be found.
	virtual std::unique_ptr<IAudioFile> OpenTrack(const std::string &path) = 0;
	// Reads the game's own file of a stream, through the handle the game passed. NULL on failure.
	virtual std::unique_ptr<IAudioFile> OpenGameFile(PlaylistStreamId id) = 0;
};

// The settings the game gives a stream, applied to every sound playing in it
struct PlaylistStreamSettings
{
	float nVolume = 1;
	float nPan = 0;
	float nPitch = 1;
	bool bMuted = false;
};

// What GetPos reports: the current track's position, length and channels
struct PlaylistStreamPosition
{
	uint64_t nFrame = 0;
	bool bLengthKnown = false;
	uint64_t nLength = 0;
	unsigned nChannels = 2;
};

class CPlaylistPlayer
{
private:
	// One sound playing in a stream
	struct Slot
	{
		std::unique_ptr<IAudioSound> lpSound;
		// The track of the stream's playlist, or -1 for the game's own file
		int nTrack = -1;
		// For messages: the track's path, or the game's name for its file
		std::string path;
		bool bLoop = false;
		// The maps loaded when the track was opened. Storm may have found it in any of them, so it has to stop when any of them is unloaded.
		std::vector<uint64_t> maps;
		bool bStarted = false;
		// Started by Update at nStartAt, after the sound before it has faded out
		bool bDelayed = false;
		uint64_t nStartAt = 0;
		double nFadeInOnStart = 0;
	};

	struct Fading
	{
		Slot slot;
		uint64_t nEndAt;
	};

	enum StreamState
	{
		// Plays the game's own file
		STREAM_GAME,
		// Plays a visit of a playlist
		STREAM_VISIT,
		// Ended, but something is still fading out; the game is told it ended once that is done
		STREAM_STOPPING,
		// The game is told the stream has ended
		STREAM_ENDED
	};

	// Where back returns to
	struct BackPoint
	{
		bool bValid = false;
		// The game's own file of the stream, or a playlist's track
		bool bGameFile = false;
		PlaylistStreamId stream = 0;
		PlaylistRef playlist;
		CPlaylistVisit visit;
		int nTrack = -1;
		bool bLoop = false;
		uint64_t nFrame = 0;
	};

	// What comes after the current track of a visit. Worked out a little before the track ends, so the next one can start without a gap, or crossfade.
	struct Step
	{
		enum Kind
		{
			// The visit's next track
			STEP_TRACK,
			// The visit has ended, and next plays a playlist
			STEP_VISIT,
			// The visit has ended, and next goes back
			STEP_BACK,
			// The visit has ended, and so has the stream, as the game will be told
			STEP_END,
			// next stops the stream
			STEP_STOP,
			// next changes the music, and the stream ends
			STEP_MUSIC
		} kind = STEP_END;

		Slot slot;
		PlaylistRef playlist;
		CPlaylistVisit visit;
		BackPoint back;
		uint64_t nBackGeneration = 0;
		PlaylistTarget target;

		double nCrossfade = 0;
		double nFadeIn = 0;
		double nFadeOut = 0;
		// The slot was scheduled to start when the current track ends
		bool bScheduled = false;
		// The current track has been faded out for fade_out
		bool bFadingOut = false;
	};

	struct Stream
	{
		PlaylistStreamId id = 0;
		bool bHasName = false;
		// Normalized
		std::string name;
		bool bMusicFile = false;
		bool bLoop = false;
		PlaylistStreamSettings settings;

		StreamState state = STREAM_ENDED;
		Slot primary;
		PlaylistRef playlist;
		CPlaylistVisit visit;
		unsigned nVisitSerial = 0;
		std::unique_ptr<Step> lpNext;
		std::vector<Fading> fading;
	};

	IAudioEngine *m_lpEngine;	// Not owned
	IPlaylistFiles *m_lpFiles;	// Not owned
	IPlaylistLog *m_lpLog;	// Not owned; may be NULL

	CPlaylistLayers m_layers;
	CPlaylistRandom m_random;
	uint64_t m_nNow;

	std::map<PlaylistStreamId, std::unique_ptr<Stream>> m_streams;

	bool m_bHasMusicStream;
	PlaylistStreamId m_musicStream;
	PlaylistRef m_current;
	BackPoint m_back;
	// Changes whenever the back point does, so a step worked out earlier can tell it is out of date
	uint64_t m_nBackGeneration;

	// Tracks that have been warned about, so each is only warned about once
	std::set<std::string> m_warnedTracks;

	unsigned m_nVisitSerial;

	// Logging
	bool Tracing() const;
	void Trace(const std::string &text);
	std::string StreamName(const Stream &stream) const;
	void WarnTrack(const PlaylistRef &playlist, int nTrack, const std::string &problem);

	Stream *Find(PlaylistStreamId id) const;
	Stream *MusicStream() const;
	bool IsMusicStream(const Stream &stream) const;

	// Sounds
	void ApplySettings(const Stream &stream, Slot &slot);
	void StartSlot(Slot &slot);
	void FadeOut(Stream &stream, Slot &slot, double nSeconds);
	void DropNext(Stream &stream);
	void DestroyGameFileSlots(Stream &stream);
	bool TimeLeft(const Stream &stream, const Slot &slot, uint64_t *lpnMilliseconds) const;

	// Opening what to play
	bool OpenTrack(const Stream &stream, const PlaylistRef &playlist, int nTrack, bool bLoop, Slot &slot);
	bool OpenVisitTrack(const Stream &stream, const PlaylistRef &playlist, CPlaylistVisit &visit, Slot &slot);
	bool StartVisit(const Stream &stream, const PlaylistRef &playlist, CPlaylistVisit &visit, Slot &slot);
	bool OpenGameFile(Stream &stream, Slot &slot);
	bool OpenBack(Stream &stream, const BackPoint &back, Slot &slot);
	BackPoint Snapshot(const Stream &stream) const;

	// Changing what plays
	void SetContent(Stream &stream, Slot &slot, double nFadeOut, double nFadeIn, double nCrossfade);
	void SetVisit(Stream &stream, const PlaylistRef &playlist, const CPlaylistVisit &visit);
	void EndStream(Stream &stream, double nFadeOut);
	void SetMusicStream(Stream &stream);
	bool PlayGameFile(Stream &stream, uint64_t nStartBytes);
	bool ApplyStartTransition(Stream &stream, const PlaylistMatch &match);
	void ApplyToMusic(const PlaylistTarget &target, bool bInterrupt, const std::string &why);
	void InterruptMusic(Stream &newStream, const PlaylistTarget &target, bool bSnapshot);
	void Reevaluate(Stream &stream);

	// Visits
	void UpdateStream(Stream &stream);
	std::unique_ptr<Step> WorkOutNext(Stream &stream);
	void TakeNext(Stream &stream, double nCrossfade);
	double PrepareWindow(const Stream &stream) const;

public:
	CPlaylistPlayer();
	// Destroys any sounds left; see Abandon
	~CPlaylistPlayer();

	// Must be called before anything else. The engine and files must outlive the player.
	void Initialize(IAudioEngine *lpEngine, IPlaylistFiles *lpFiles, IPlaylistLog *lpLog, uint64_t nSeed);

	// The playlist files. Without a base file, streams play the game's files unless a map's file says otherwise.
	void SetBase(std::shared_ptr<const PlaylistFile> base);
	bool MapPlaylistsAllowed() const
	{ return m_layers.MapsAllowed(); }
	bool HasPlaylists() const
	{ return !m_layers.Empty(); }
	// A map's file, laid over the others while its archive (the tag) is open. Warns about names neither it nor the base file defines. False if maps' files aren't allowed.
	bool PushMap(uintptr_t tag, std::shared_ptr<const PlaylistFile> map);
	// Stops every track opened while the map was loaded, and works out again what the music stream should play
	bool PopMap(uintptr_t tag);
	bool HasMap(uintptr_t tag) const
	{ return m_layers.HasMap(tag); }

	// The game starts a stream. lpName is the file's name, if known; without one, the game's file plays with no transitions. nStartBytes is where in the game's file to start, in bytes of 16-bit PCM. Returns false if nothing could be played.
	bool Begin(PlaylistStreamId id, const std::string *lpName, bool bLoop, uint64_t nStartBytes, const PlaylistStreamSettings &settings);
	// The game ends a stream. False if there is no such stream.
	bool End(PlaylistStreamId id);
	void EndAll();
	// Leaves every sound to the operating system without destroying it, for when destroying could deadlock (in DllMain)
	void Abandon();

	bool HasStream(PlaylistStreamId id) const
	{ return Find(id) != NULL; }
	// Whether the game should see the stream as still playing
	bool IsPlaying(PlaylistStreamId id) const;
	bool GetPosition(PlaylistStreamId id, PlaylistStreamPosition *lpPosition) const;

	void SetVolume(PlaylistStreamId id, float nVolume);
	void SetPan(PlaylistStreamId id, float nPan);
	void SetPitch(PlaylistStreamId id, float nRatio);
	void SetMuted(PlaylistStreamId id, bool bMuted);

	// Called periodically: moves visits on to their next tracks, finishes fades, and notices streams that have ended
	void Update(uint64_t nNowMs);

	// For the simulator and tests
	PlaylistRef CurrentPlaylist() const
	{ return m_current; }
	bool GetMusicStream(PlaylistStreamId *lpId) const;
	// The sound the stream's position comes from, if any
	IAudioSound *CurrentSound(PlaylistStreamId id) const;
	// How many milliseconds until what the stream plays next starts, once what played before has faded out; 0 if nothing is waiting
	uint64_t WaitingFor(PlaylistStreamId id) const;
	// Changes each time a new visit starts in the stream
	unsigned VisitSerial(PlaylistStreamId id) const;
	// The playlist a stream is visiting, if any
	PlaylistRef StreamPlaylist(PlaylistStreamId id) const;
	// "'music\t1.ogg' of \"terranlist\"", "the game's file", "ended"
	std::string DescribeStream(PlaylistStreamId id) const;
	bool HasBackPoint() const
	{ return m_back.bValid; }
};

#endif // #ifndef PLAYLISTPLAYER_H
