/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2009 Justin Olbrantz. All Rights Reserved.
*/

#include "DdaPlayer.h"
#include "DdaConversions.h"
#include "PlaylistPath.h"

#include <assert.h>
#include <limits.h>
#include <string.h>
#include <new>

#include <QDebug.h>

namespace
{
	class CLock
	{
	private:
		CRITICAL_SECTION &m_cs;

	public:
		explicit CLock(CRITICAL_SECTION &cs) : m_cs(cs)
		{ EnterCriticalSection(&m_cs); }

		~CLock()
		{ LeaveCriticalSection(&m_cs); }
	};

	// Reads a sound file through the Storm SFile functions
	class CStormAudioFile : public IAudioFile
	{
	private:
		HANDLE m_hSFile;
		STORMFILEFUNCTIONS m_storm;
		// Files ThunderGraft opened itself, for playlists, are closed with the sound; the game's are the game's
		bool m_bOwnsFile;

	public:
		CStormAudioFile(HANDLE hSFile, const STORMFILEFUNCTIONS &storm, bool bOwnsFile = false)
			: m_hSFile(hSFile), m_storm(storm), m_bOwnsFile(bOwnsFile)
		{
			// The game may have read from the file before handing it to us
			m_storm.SFileSetFilePointer(m_hSFile, 0, NULL, FILE_BEGIN);
		}

		virtual ~CStormAudioFile()
		{
			if (m_bOwnsFile)
				m_storm.SFileCloseFile(m_hSFile);
		}

		virtual bool Read(void *lpBuffer, size_t nBytes, size_t *lpnBytesRead)
		{
			assert(lpnBytesRead);

			if (nBytes > MAXDWORD)
				nBytes = MAXDWORD;

			// Like ReadFile, SFileReadFile fails with ERROR_HANDLE_EOF when it reads less than requested, but still reports how much it did read
			DWORD nBytesRead = 0;
			BOOL bSuccess = m_storm.SFileReadFile(m_hSFile, lpBuffer, (DWORD)nBytes, &nBytesRead, NULL);
			if (!bSuccess && GetLastError() != ERROR_HANDLE_EOF)
				return false;

			*lpnBytesRead = (nBytesRead <= nBytes ? nBytesRead : nBytes);

			return true;
		}

		virtual bool Seek(int64_t nOffset, AudioSeekOrigin origin)
		{
			if (nOffset < LONG_MIN || nOffset > LONG_MAX)
				return false;

			DWORD dwMoveMethod = FILE_BEGIN;
			if (origin == AUDIO_SEEK_CURRENT)
				dwMoveMethod = FILE_CURRENT;
			else if (origin == AUDIO_SEEK_END)
				dwMoveMethod = FILE_END;

			return m_storm.SFileSetFilePointer(m_hSFile, (long)nOffset, NULL, dwMoveMethod) != INVALID_SET_FILE_POINTER;
		}

		virtual bool Tell(int64_t *lpnPosition)
		{
			assert(lpnPosition);

			DWORD nPosition = m_storm.SFileSetFilePointer(m_hSFile, 0, NULL, FILE_CURRENT);
			if (nPosition == INVALID_SET_FILE_POINTER)
				return false;

			*lpnPosition = nPosition;

			return true;
		}

		virtual bool GetSize(uint64_t *lpnSize)
		{
			assert(lpnSize);

			DWORD nSize = m_storm.SFileGetFileSize(m_hSFile, NULL);
			if (nSize == INVALID_FILE_SIZE)
				return false;

			*lpnSize = nSize;

			return true;
		}
	};

	// Reads a file on disk, for playlist tracks with absolute paths, which a playlist file on disk may name
	class CDiskAudioFile : public IAudioFile
	{
	private:
		HANDLE m_hFile;

	public:
		explicit CDiskAudioFile(HANDLE hFile) : m_hFile(hFile) {}

		virtual ~CDiskAudioFile()
		{ CloseHandle(m_hFile); }

		static std::unique_ptr<IAudioFile> Open(const std::string &path)
		{
			HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (hFile == INVALID_HANDLE_VALUE)
				return nullptr;

			std::unique_ptr<IAudioFile> lpFile(new (std::nothrow) CDiskAudioFile(hFile));
			if (!lpFile)
				CloseHandle(hFile);

			return lpFile;
		}

		virtual bool Read(void *lpBuffer, size_t nBytes, size_t *lpnBytesRead)
		{
			assert(lpnBytesRead);

			if (nBytes > MAXDWORD)
				nBytes = MAXDWORD;

			DWORD nBytesRead = 0;
			if (!ReadFile(m_hFile, lpBuffer, (DWORD)nBytes, &nBytesRead, NULL))
				return false;

			*lpnBytesRead = nBytesRead;
			return true;
		}

		virtual bool Seek(int64_t nOffset, AudioSeekOrigin origin)
		{
			LARGE_INTEGER offset;
			offset.QuadPart = nOffset;
			DWORD dwMoveMethod = (origin == AUDIO_SEEK_CURRENT) ? FILE_CURRENT : (origin == AUDIO_SEEK_END) ? FILE_END : FILE_BEGIN;
			return SetFilePointerEx(m_hFile, offset, NULL, dwMoveMethod) != FALSE;
		}

		virtual bool Tell(int64_t *lpnPosition)
		{
			LARGE_INTEGER zero, position;
			zero.QuadPart = 0;
			if (!SetFilePointerEx(m_hFile, zero, &position, FILE_CURRENT))
				return false;

			*lpnPosition = position.QuadPart;
			return true;
		}

		virtual bool GetSize(uint64_t *lpnSize)
		{
			LARGE_INTEGER size;
			if (!GetFileSizeEx(m_hFile, &size))
				return false;

			*lpnSize = (uint64_t)size.QuadPart;
			return true;
		}
	};

	// How the playlist player opens files: tracks through Storm, as the game opens its own, so they can be in archives or on disk; the game's files through the game's handles
	class CStormPlaylistFiles : public IPlaylistFiles
	{
	private:
		STORMFILEFUNCTIONS m_storm;

	public:
		explicit CStormPlaylistFiles(const STORMFILEFUNCTIONS &storm) : m_storm(storm) {}

		virtual std::unique_ptr<IAudioFile> OpenTrack(const std::string &path)
		{
			// The playlist file's reader only lets files on disk name absolute paths. Storm might not open those, so they're opened directly.
			if (IsAbsolutePlaylistPath(path))
				return CDiskAudioFile::Open(path);

			if (!m_storm.SFileOpenFileEx)
				return nullptr;

			HANDLE hFile = NULL;
			if (!m_storm.SFileOpenFileEx(NULL, PlaylistPathForStorm(path).c_str(), 0, &hFile) || !hFile)
				return nullptr;

			std::unique_ptr<IAudioFile> lpFile(new (std::nothrow) CStormAudioFile(hFile, m_storm, true));
			if (!lpFile)
				m_storm.SFileCloseFile(hFile);

			return lpFile;
		}

		virtual std::unique_ptr<IAudioFile> OpenGameFile(PlaylistStreamId id)
		{
			return std::unique_ptr<IAudioFile>(new (std::nothrow) CStormAudioFile((HANDLE)id, m_storm));
		}
	};

	PlaylistStreamId StreamId(HANDLE hSFile)
	{
		return (PlaylistStreamId)hSFile;
	}
}

CDdaPlayer::CDdaPlayer()
{
	memset(&m_storm, 0, sizeof(m_storm));
	m_lpEngine = NULL;
	m_lpNames = NULL;
	m_nLastTick = 0;
	m_nNow = 0;
	m_bActivated = TRUE;

	InitializeCriticalSection(&m_csLock);
}

CDdaPlayer::~CDdaPlayer()
{
	// The player lives in a global object, so this runs inside DllMain when the process exits. Destroying sounds there could deadlock waiting for the audio engine's threads, so we deliberately leave anything still playing for Windows to clean up with the process. Callers that want sounds stopped call StopAll first.
	m_playlist.Abandon();
	m_lpFiles.release();
}

void CDdaPlayer::Initialize(IN const STORMFILEFUNCTIONS &storm, IN IAudioEngine *lpEngine, OPTIONAL IN CStormFileNames *lpNames, OPTIONAL IN IPlaylistLog *lpLog)
{
	assert(storm.SFileCloseFile && storm.SFileReadFile && storm.SFileGetFileSize && storm.SFileSetFilePointer);
	assert(lpEngine);

	CLock lock(m_csLock);

	m_storm = storm;
	m_lpEngine = lpEngine;
	m_lpNames = lpNames;
	m_lpFiles.reset(new CStormPlaylistFiles(storm));

	// Shuffles differ from one session to the next
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	uint64_t nSeed = (uint64_t)counter.QuadPart ^ ((uint64_t)GetCurrentProcessId() << 32) ^ GetTickCount();

	m_playlist.Initialize(lpEngine, m_lpFiles.get(), lpLog, nSeed);
	m_nLastTick = GetTickCount();
}

void CDdaPlayer::SetBasePlaylist(IN std::shared_ptr<const PlaylistFile> base)
{
	CLock lock(m_csLock);
	m_playlist.SetBase(base);
}

BOOL CDdaPlayer::MapPlaylistsAllowed()
{
	CLock lock(m_csLock);
	return m_playlist.MapPlaylistsAllowed();
}

BOOL CDdaPlayer::PushMapPlaylist(IN HANDLE hMpq, IN std::shared_ptr<const PlaylistFile> map)
{
	CLock lock(m_csLock);
	return m_playlist.PushMap((uintptr_t)hMpq, map);
}

BOOL CDdaPlayer::PopMapPlaylist(IN HANDLE hMpq)
{
	CLock lock(m_csLock);
	return m_playlist.PopMap((uintptr_t)hMpq);
}

BOOL CDdaPlayer::Begin(IN HANDLE hSFile, IN DWORD dwFlags, IN DWORD dwBufferBytes, OPTIONAL IN DWORD dwStartPosBytes, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos, OPTIONAL IN IDirectSoundBuffer *lpSoundBuffer)
{
	QDebugWriteEntry("CDdaPlayer::Begin(%p, 0x%lX, %lu, %lu, %ld, %ld, %p)", hSFile, dwFlags, dwBufferBytes, dwStartPosBytes, nVolume, nPanPos, lpSoundBuffer);

	if (!hSFile || !dwBufferBytes)
		return FALSE;

	CLock lock(m_csLock);

	assert(m_lpEngine);

	// If the sound is already playing, we need to stop it so we can restart it
	StopSound(hSFile);

	PLAYINGSOUND newSound;
	DWORD nFrequency = SFILE_DDA_DEFAULT;

	// If an IDirectSoundBuffer is provided, Storm uses that instead of creating its own buffer. While we don't use the buffer we get passed to actually play the music, we have to make the game think we're using it.
	if (lpSoundBuffer)
	{
		// To start with, make sure it's stopped
		lpSoundBuffer->Stop();

		// We need to adopt the settings of the buffer for our sound. However, if a volume and pan position are also provided, Storm will update the buffer with those settings, instead.
		if (nVolume == SFILE_DDA_DEFAULT)
			lpSoundBuffer->GetVolume(&nVolume);
		else
			lpSoundBuffer->SetVolume(nVolume);

		if (nPanPos == SFILE_DDA_DEFAULT)
			lpSoundBuffer->GetPan(&nPanPos);
		else
			lpSoundBuffer->SetPan(nPanPos);

		if (FAILED(lpSoundBuffer->GetFrequency(&nFrequency)))
			nFrequency = SFILE_DDA_DEFAULT;

		// Get the sampling rate and caps
		DSBCAPS bufferCaps;
		WAVEFORMATEX bufferFormat;
		DWORD nBytesWritten = 0;

		memset(&bufferCaps, 0, sizeof(bufferCaps));
		memset(&bufferFormat, 0, sizeof(bufferFormat));
		bufferCaps.dwSize = sizeof(bufferCaps);
		if (FAILED(lpSoundBuffer->GetCaps(&bufferCaps)) ||
			FAILED(lpSoundBuffer->GetFormat(&bufferFormat, sizeof(bufferFormat), &nBytesWritten)) ||
			nBytesWritten < sizeof(PCMWAVEFORMAT))
		{
			QDebugWriteEntry("CDdaPlayer::Begin : GetCaps or GetFormat failed!");
			return FALSE;
		}

		newSound.bGlobal = (bufferCaps.dwFlags & DSBCAPS_GLOBALFOCUS) != 0;
		newSound.nBufferFrequency = bufferFormat.nSamplesPerSec;

		QDebugWriteEntry("CDdaPlayer::Begin : IDirectSoundBuffer format 0x%X, caps 0x%lX, buffer size %lu bytes, %lu samples per second, %u bits per sample, %u channels", bufferFormat.wFormatTag, bufferCaps.dwFlags, bufferCaps.dwBufferBytes, bufferFormat.nSamplesPerSec, bufferFormat.wBitsPerSample, bufferFormat.nChannels);

		// We have no guarantee that the buffer that was given to us is empty. Since the game may play it, we need to ensure that it's silence. So we'll lock it and "zero" it here.
		LPVOID lpBuffer = NULL;
		DWORD nBufferSize = 0;
		if (SUCCEEDED(lpSoundBuffer->Lock(0, 0, &lpBuffer, &nBufferSize, NULL, NULL, DSBLOCK_ENTIREBUFFER)))
		{
			// Depending on whether it's 8-bit or 16-bit PCM, we'll need to fill it with different values. 8-bit PCM is unsigned, so we need to fill it with 0x80; 16-bit PCM is signed, so we need to fill it with 0x0000.
			BYTE zero = (bufferFormat.wBitsPerSample == 8) ? 0x80 : 0;
			memset(lpBuffer, zero, nBufferSize);

			lpSoundBuffer->Unlock(lpBuffer, nBufferSize, NULL, 0);
		}
	}

	newSound.nVolume = (nVolume != SFILE_DDA_DEFAULT ? nVolume : DDA_VOLUME_MAX);
	newSound.nPanPos = (nPanPos != SFILE_DDA_DEFAULT ? nPanPos : 0);
	newSound.nFrequency = nFrequency;

	PlaylistStreamSettings settings;
	settings.nVolume = DdaVolumeToLinear(newSound.nVolume);
	settings.nPan = DdaPanToLinear(newSound.nPanPos);
	if (lpSoundBuffer && nFrequency != SFILE_DDA_DEFAULT)
		settings.nPitch = DdaPitchRatio(nFrequency, newSound.nBufferFrequency);
	settings.bMuted = !newSound.bGlobal && !m_bActivated;

	QDebugWriteEntry("CDdaPlayer::Begin : Final volume %ld, pan %ld, frequency %lu, is %sglobal", newSound.nVolume, newSound.nPanPos, newSound.nFrequency, (newSound.bGlobal ? "" : "not "));

	// The name the game opened the file with decides whether a playlist plays in its place. A file whose name isn't known plays as the game asked.
	std::string name;
	bool bHasName = false;
	try
	{ bHasName = m_lpNames && m_lpNames->Lookup(hSFile, &name); }
	catch (...)
	{ bHasName = false; }

	try
	{
		if (!m_playlist.Begin(StreamId(hSFile), bHasName ? &name : NULL, (dwFlags & SFILE_DDA_LOOPING) != 0, dwStartPosBytes, settings))
			return FALSE;
	}
	catch (...)
	{
		m_playlist.End(StreamId(hSFile));
		return FALSE;
	}

	// The sound is playing fine. Add it to the list and we're done. If that fails, the sound is ended on the way out.
	try
	{
		newSound.lpSoundBuffer = lpSoundBuffer;
		m_playingSoundMap.emplace(hSFile, std::move(newSound));
	}
	catch (...)
	{
		m_playlist.End(StreamId(hSFile));
		return FALSE;
	}

	if (lpSoundBuffer)
	{
		// Finally, note that we're storing a reference to the IDirectSoundBuffer, and start "playing" it, so the game can see that the sound is playing. Note that as a streaming audio system, DDA always works with looping buffers, regardless of whether the sound itself is looped.
		ULONG nRefCount = lpSoundBuffer->AddRef();
		HRESULT hResult = lpSoundBuffer->Play(0, 0, DSBPLAY_LOOPING);

		QDebugWriteEntry("CDdaPlayer::Begin : Buffer AddRef returned %lu, Play returned 0x%lX", nRefCount, hResult);
		(void)nRefCount;
		(void)hResult;
	}

	return TRUE;
}

BOOL CDdaPlayer::End(IN HANDLE hSFile)
{
	QDebugWriteEntry("CDdaPlayer::End(%p)", hSFile);

	if (!hSFile)
		return FALSE;

	CLock lock(m_csLock);

	return StopSound(hSFile);
}

BOOL CDdaPlayer::StopSound(IN HANDLE hSFile)
{
	QDebugWriteEntry("CDdaPlayer::StopSound(%p)", hSFile);

	// See if the sound is playing
	PlayingSoundMap::iterator itSound = m_playingSoundMap.find(hSFile);
	if (itSound == m_playingSoundMap.end())
		return FALSE;

	// If necessary, stop the IDirectSoundBuffer and release our reference
	PLAYINGSOUND &sound = itSound->second;
	if (sound.lpSoundBuffer)
	{
		HRESULT hResult = sound.lpSoundBuffer->Stop();
		ULONG nRefCount = sound.lpSoundBuffer->Release();

		QDebugWriteEntry("CDdaPlayer::StopSound : Buffer Stop returned 0x%lX, Release returned %lu", hResult, nRefCount);
		(void)hResult;
		(void)nRefCount;
	}

	// End the stream, then close the file if the game already tried to. The sounds have to go first, as they read from the file.
	BOOL bCloseOnStop = sound.bCloseOnStop;
	m_playlist.End(StreamId(hSFile));
	m_playingSoundMap.erase(itSound);

	if (bCloseOnStop)
		m_storm.SFileCloseFile(hSFile);

	return TRUE;
}

void CDdaPlayer::StopAll()
{
	QDebugWriteEntry("CDdaPlayer::StopAll()");

	CLock lock(m_csLock);

	while (!m_playingSoundMap.empty())
		StopSound(m_playingSoundMap.begin()->first);
}

BOOL CDdaPlayer::GetPos(IN HANDLE hSFile, OPTIONAL OUT LPDWORD lpdwCurPos, OPTIONAL OUT LPDWORD lpdwLength)
{
	if (!hSFile)
		return FALSE;

	CLock lock(m_csLock);

	// There are a few different possibilities here. It could be a file that wasn't ever played, it could be playing right now, or it could have started but finished.
	PlayingSoundMap::iterator itSound = m_playingSoundMap.find(hSFile);
	if (itSound == m_playingSoundMap.end())
		return FALSE;

	// If the sound has finished, return false to communicate this. With a playlist, that's when its visit ends rather than any one track; the position and length are the current track's.
	PlaylistStreamPosition position;
	if (!m_playlist.IsPlaying(StreamId(hSFile)) || !m_playlist.GetPosition(StreamId(hSFile), &position))
		return FALSE;

	// For some files the length is only approximate or not known at all. That means it's possible that the current position >= the length. Need to handle this case to make sure the position is always < the length in the values we return.
	uint64_t nCurPos = position.nFrame, nLength = position.nLength;
	if (!position.bLengthKnown || nCurPos >= nLength)
		nLength = nCurPos + 1;

	uint64_t nCurPosBytes = DdaFramesToBytes(nCurPos, position.nChannels),
		nLengthBytes = DdaFramesToBytes(nLength, position.nChannels);

	// The DDA functions can't express positions beyond 4 GB of PCM, about 7 hours of 44 khz stereo
	if (nLengthBytes > MAXDWORD)
		nLengthBytes = MAXDWORD;
	if (nCurPosBytes >= nLengthBytes)
		nCurPosBytes = nLengthBytes - 1;

	if (lpdwCurPos)
		*lpdwCurPos = (DWORD)nCurPosBytes;
	if (lpdwLength)
		*lpdwLength = (DWORD)nLengthBytes;

	return TRUE;
}

// Unlike SFileDdaGetPos, SFileDdaGetVolume and SFileDdaSetVolume do not fail when the sound has finished playing
BOOL CDdaPlayer::GetVolume(IN HANDLE hSFile, OPTIONAL OUT long *lpnVolume, OPTIONAL OUT long *lpnPanPos)
{
	if (!hSFile)
		return FALSE;

	CLock lock(m_csLock);

	PlayingSoundMap::iterator itSound = m_playingSoundMap.find(hSFile);
	if (itSound == m_playingSoundMap.end())
		return FALSE;

	if (lpnVolume)
		*lpnVolume = itSound->second.nVolume;
	if (lpnPanPos)
		*lpnPanPos = itSound->second.nPanPos;

	return TRUE;
}

BOOL CDdaPlayer::SetVolume(IN HANDLE hSFile, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos)
{
	QDebugWriteEntry("CDdaPlayer::SetVolume(%p, %ld, %ld)", hSFile, nVolume, nPanPos);

	if (!hSFile)
		return FALSE;

	CLock lock(m_csLock);

	PlayingSoundMap::iterator itSound = m_playingSoundMap.find(hSFile);
	if (itSound == m_playingSoundMap.end())
		return FALSE;

	// Storm would set these on the IDirectSoundBuffer, if the game supplied one. Doing the same keeps Update from mistaking the buffer's old settings for a change.
	PLAYINGSOUND &sound = itSound->second;
	if (nVolume != SFILE_DDA_DEFAULT)
	{
		sound.nVolume = nVolume;
		m_playlist.SetVolume(StreamId(hSFile), DdaVolumeToLinear(nVolume));
		if (sound.lpSoundBuffer)
			sound.lpSoundBuffer->SetVolume(nVolume);
	}
	if (nPanPos != SFILE_DDA_DEFAULT)
	{
		sound.nPanPos = nPanPos;
		m_playlist.SetPan(StreamId(hSFile), DdaPanToLinear(nPanPos));
		if (sound.lpSoundBuffer)
			sound.lpSoundBuffer->SetPan(nPanPos);
	}

	return TRUE;
}

BOOL CDdaPlayer::CloseFile(IN HANDLE hSFile)
{
	if (!hSFile)
		return FALSE;

	{
		CLock lock(m_csLock);

		// If it's a playing sound, set it to close on stop. If not, close it now.
		PlayingSoundMap::iterator itSound = m_playingSoundMap.find(hSFile);
		if (itSound != m_playingSoundMap.end())
		{
			itSound->second.bCloseOnStop = TRUE;

			return TRUE;
		}
	}

	return m_storm.SFileCloseFile(hSFile);
}

void CDdaPlayer::UpdateSound(IN HANDLE hSFile, IN PLAYINGSOUND &sound)
{
	if (!sound.bIsPlaying)
		return;

	if (!m_playlist.IsPlaying(StreamId(hSFile)))
	{
		// The sound just stopped playing. Update our state as necessary. If we have a IDirectSoundBuffer, stop it so the game can see the sound is done.
		QDebugWriteEntry("CDdaPlayer::UpdateSound : Sound has completed");

		if (sound.lpSoundBuffer)
			sound.lpSoundBuffer->Stop();

		sound.bIsPlaying = FALSE;

		return;
	}

	if (sound.lpSoundBuffer)
	{
		// Copy any changes the game made to the volume, panning, and frequency
		long nVolume, nPanPos;
		DWORD nFrequency;

		if (SUCCEEDED(sound.lpSoundBuffer->GetVolume(&nVolume)) &&
			nVolume != sound.nVolume)
		{
			QDebugWriteEntry("CDdaPlayer::UpdateSound : Updating volume from %ld to %ld", sound.nVolume, nVolume);

			m_playlist.SetVolume(StreamId(hSFile), DdaVolumeToLinear(nVolume));
			sound.nVolume = nVolume;
		}
		if (SUCCEEDED(sound.lpSoundBuffer->GetPan(&nPanPos)) &&
			nPanPos != sound.nPanPos)
		{
			QDebugWriteEntry("CDdaPlayer::UpdateSound : Updating panning from %ld to %ld", sound.nPanPos, nPanPos);

			m_playlist.SetPan(StreamId(hSFile), DdaPanToLinear(nPanPos));
			sound.nPanPos = nPanPos;
		}
		if (SUCCEEDED(sound.lpSoundBuffer->GetFrequency(&nFrequency)) &&
			nFrequency != sound.nFrequency)
		{
			QDebugWriteEntry("CDdaPlayer::UpdateSound : Updating frequency from %lu to %lu", sound.nFrequency, nFrequency);

			m_playlist.SetPitch(StreamId(hSFile), DdaPitchRatio(nFrequency, sound.nBufferFrequency));
			sound.nFrequency = nFrequency;
		}
	}
}

void CDdaPlayer::Update(IN BOOL bActivated)
{
	CLock lock(m_csLock);

	BOOL bActivationChanged = (bActivated != m_bActivated);
	if (bActivationChanged)
		QDebugWriteEntry("CDdaPlayer::Update : Activation status changed from %d to %d", m_bActivated, bActivated);

	// Playlists move on to their next tracks, and fades finish. GetTickCount wraps every 49 days; the difference doesn't.
	DWORD nTick = GetTickCount();
	m_nNow += (DWORD)(nTick - m_nLastTick);
	m_nLastTick = nTick;
	try
	{ m_playlist.Update(m_nNow); }
	catch (...)
	{
		// Out of memory; try again next time
	}

	for (PlayingSoundMap::iterator itSound = m_playingSoundMap.begin(); itSound != m_playingSoundMap.end(); itSound++)
	{
		PLAYINGSOUND &sound = itSound->second;

		UpdateSound(itSound->first, sound);

		// Mute/unmute non-global sounds
		if (bActivationChanged && !sound.bGlobal)
			m_playlist.SetMuted(StreamId(itSound->first), !bActivated);
	}

	m_bActivated = bActivated;
}
