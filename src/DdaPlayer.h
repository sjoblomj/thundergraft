/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2009 Justin Olbrantz. All Rights Reserved.
*/

/*
	The Storm DDA system (SFileDda) is a small audio streaming system used for music and other sounds that are streamed (decoded on the fly). The system essentially consists of a streaming thread which asynchronously reads data from the files and copies it into the IDirectSoundBuffers. No decoding is necessary because the file is, after decompression by the MPQ decompression code, in PCM format, directly playable by the sound card once placed in the buffer. As such, the system is a fairly thin wrapper around DirectSound.

	The DDA system is initialized with an IDirectSound allocated and initialized by the host game. All DDA API functions refer to sounds by their SFile handle, which must be opened previously either from disk or from MPQ using the Storm SFile functions, and most flags and parameters (volume, panning, etc.) are specified in the native format of DirectSound. Reference-counting is used to ensure that SFileCloseFile does not close a file that is still playing (or, unrelated, still has outstanding asynchronous I/O requests) - the DDA system holds one reference on the file until the SFileDdaEnd function is called on it.

	IDirectSoundBuffers used to stream the sound are either created by SFileDdaBegin/Ex or passed as a parameter to SFileDdaBeginEx. If passed as a parameter, the game may update parameters of the stream such as volume directly through the IDirectSoundBuffer interface, rather than through the equivalent DDA functions such as SFileDdaSetVolume, which saves the overhead of locking the DDA mutex and looking up the IDirectSoundBuffer corresponding to the referenced SFile handle; Diablo II has also been observed to call GetStatus to determine if the sound is yet/still playing. To support this, we must emulate game-provided IDirectSoundBuffers by synchronizing the state between the buffer and the actual sound being played.

	CDdaPlayer implements the DDA functions on top of an IAudioEngine. It knows nothing about hooking; the hook functions in CThunderGraftPlugin forward to it. What each stream plays, the game's file or a playlist in its place, is up to its CPlaylistPlayer (see PlaylistPlayer.h); CDdaPlayer looks after what the game sees of it.
*/

#ifndef DDAPLAYER_H
#define DDAPLAYER_H

#include <windows.h>
#include <dsound.h>
#include <memory>
#include <unordered_map>

#include "AudioEngine.h"
#include "PlaylistPlayer.h"
#include "Storm.h"
#include "StormFileNames.h"

struct PLAYINGSOUND
{
	// Normal, non-global sounds automatically mute when the program playing them is minimized or loses focus. Global sounds continue playing even in these conditions.
	BOOL bGlobal;
	// The DDA system holds a reference to the currently playing file until SFileDdaEnd is called; this means that the file will not be freed when SFileCloseFile is called, if the DDA system still has a pointer to it. Since we can't access the reference count (that's a game-dependent internal field of the HSFILE), we have to emulate it by deferring the call to SFileCloseFile until we're done playing.
	BOOL bCloseOnStop;

	// Whether the sounds is still playing or has stopped. This is used to determine when a sound has just stopped.
	BOOL bIsPlaying;

	// The IDirectSoundBuffer we will get volume, pan, and frequency settings from, if it exists. We hold a reference on it.
	IDirectSoundBuffer *lpSoundBuffer;

	// The current values, in DirectSound units. Also used to determine whether the game changed them on lpSoundBuffer.
	long nVolume;
	long nPanPos;
	DWORD nFrequency;

	// The sampling rate of lpSoundBuffer, which nFrequency is relative to
	DWORD nBufferFrequency;

	PLAYINGSOUND()
	{
		bGlobal = FALSE;
		bCloseOnStop = FALSE;
		bIsPlaying = TRUE;

		lpSoundBuffer = NULL;

		nVolume = SFILE_DDA_DEFAULT;
		nPanPos = SFILE_DDA_DEFAULT;
		nFrequency = SFILE_DDA_DEFAULT;
		nBufferFrequency = 0;
	}
};

// Everything is protected by m_csLock, so all functions may be called from any thread
class CDdaPlayer
{
private:
	CRITICAL_SECTION m_csLock;

	STORMFILEFUNCTIONS m_storm;
	IAudioEngine *m_lpEngine;	// Not owned
	CStormFileNames *m_lpNames;	// Not owned; may be NULL

	// Opens the files the playlist player plays
	std::unique_ptr<IPlaylistFiles> m_lpFiles;
	// What each stream plays. Streams are identified by their SFile handles.
	CPlaylistPlayer m_playlist;
	// GetTickCount's milliseconds, made 64-bit
	DWORD m_nLastTick;
	uint64_t m_nNow;

	// Map to lookup playing sounds from their SFile handles
	typedef std::unordered_map<HANDLE, PLAYINGSOUND> PlayingSoundMap;
	PlayingSoundMap m_playingSoundMap;

	// Whether the game is in the foreground, as of the last call to Update
	BOOL m_bActivated;

	BOOL StopSound(IN HANDLE hSFile);
	void UpdateSound(IN HANDLE hSFile, IN PLAYINGSOUND &sound);

public:
	CDdaPlayer();
	// Deliberately leaves any sounds still playing alone; see the comment in the implementation
	~CDdaPlayer();

	// Must be called before anything else. The engine, the names and the log must outlive the player. Without names, every stream plays the game's file.
	void Initialize(IN const STORMFILEFUNCTIONS &storm, IN IAudioEngine *lpEngine, OPTIONAL IN CStormFileNames *lpNames = NULL, OPTIONAL IN IPlaylistLog *lpLog = NULL);

	// The playlist files; see CPlaylistPlayer
	void SetBasePlaylist(IN std::shared_ptr<const PlaylistFile> base);
	BOOL MapPlaylistsAllowed();
	BOOL PushMapPlaylist(IN HANDLE hMpq, IN std::shared_ptr<const PlaylistFile> map);
	BOOL PopMapPlaylist(IN HANDLE hMpq);

	// The DDA functions. Parameters and return values are as for the Storm functions of the same names.
	BOOL Begin(IN HANDLE hSFile, IN DWORD dwFlags, IN DWORD dwBufferBytes, OPTIONAL IN DWORD dwStartPosBytes, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos, OPTIONAL IN IDirectSoundBuffer *lpSoundBuffer);
	BOOL End(IN HANDLE hSFile);
	BOOL GetPos(IN HANDLE hSFile, OPTIONAL OUT LPDWORD lpdwCurPos, OPTIONAL OUT LPDWORD lpdwLength);
	BOOL GetVolume(IN HANDLE hSFile, OPTIONAL OUT long *lpnVolume, OPTIONAL OUT long *lpnPanPos);
	BOOL SetVolume(IN HANDLE hSFile, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos);

	// SFileCloseFile, deferred until the sound is ended if the file is playing
	BOOL CloseFile(IN HANDLE hSFile);

	// Ends all sounds, as SFileDdaDestroy does
	void StopAll();

	// Called periodically: notices sounds that have finished, copies changes the game made to its IDirectSoundBuffers, and mutes or unmutes non-global sounds when the game loses or regains the foreground
	void Update(IN BOOL bActivated);
};

#endif // #ifndef DDAPLAYER_H
