/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2009 Justin Olbrantz. All Rights Reserved.
*/

#include "DdaPlayer.h"
#include "DdaConversions.h"

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

	public:
		CStormAudioFile(HANDLE hSFile, const STORMFILEFUNCTIONS &storm)
			: m_hSFile(hSFile), m_storm(storm)
		{
			// The game may have read from the file before handing it to us
			m_storm.SFileSetFilePointer(m_hSFile, 0, NULL, FILE_BEGIN);
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
}

CDdaPlayer::CDdaPlayer()
{
	memset(&m_storm, 0, sizeof(m_storm));
	m_lpEngine = NULL;
	m_bActivated = TRUE;

	InitializeCriticalSection(&m_csLock);
}

CDdaPlayer::~CDdaPlayer()
{
	// The player lives in a global object, so this runs inside DllMain when the process exits. Destroying sounds there could deadlock waiting for the audio engine's threads, so we deliberately leave anything still playing for Windows to clean up with the process. Callers that want sounds stopped call StopAll first.
	for (PlayingSoundMap::iterator itSound = m_playingSoundMap.begin(); itSound != m_playingSoundMap.end(); itSound++)
		itSound->second.lpSound.release();
}

void CDdaPlayer::Initialize(IN const STORMFILEFUNCTIONS &storm, IN IAudioEngine *lpEngine)
{
	assert(storm.SFileCloseFile && storm.SFileReadFile && storm.SFileGetFileSize && storm.SFileSetFilePointer);
	assert(lpEngine);

	CLock lock(m_csLock);

	m_storm = storm;
	m_lpEngine = lpEngine;
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

	// Open the stream. Essentially this opens the file in preparation for playing.
	std::unique_ptr<IAudioFile> lpFile(new (std::nothrow) CStormAudioFile(hSFile, m_storm));
	if (!lpFile)
		return FALSE;

	newSound.lpSound = m_lpEngine->OpenSound(std::move(lpFile), (dwFlags & SFILE_DDA_LOOPING) != 0);
	QDebugWriteEntry("CDdaPlayer::Begin : OpenSound returned %p", newSound.lpSound.get());
	if (!newSound.lpSound)
		return FALSE;

	IAudioSound &sound = *newSound.lpSound;

	if (dwStartPosBytes)
		sound.SeekToFrame(DdaBytesToFrames(dwStartPosBytes, sound.GetChannels()));

	newSound.nVolume = (nVolume != SFILE_DDA_DEFAULT ? nVolume : DDA_VOLUME_MAX);
	newSound.nPanPos = (nPanPos != SFILE_DDA_DEFAULT ? nPanPos : 0);
	newSound.nFrequency = nFrequency;

	sound.SetVolume(DdaVolumeToLinear(newSound.nVolume));
	sound.SetPan(DdaPanToLinear(newSound.nPanPos));
	if (lpSoundBuffer && nFrequency != SFILE_DDA_DEFAULT)
		sound.SetPitch(DdaPitchRatio(nFrequency, newSound.nBufferFrequency));
	if (!newSound.bGlobal && !m_bActivated)
		sound.SetMuted(true);

	QDebugWriteEntry("CDdaPlayer::Begin : Final volume %ld, pan %ld, frequency %lu, is %sglobal", newSound.nVolume, newSound.nPanPos, newSound.nFrequency, (newSound.bGlobal ? "" : "not "));

	if (!sound.Start())
		return FALSE;

	// The sound is playing fine. Add it to the list and we're done. If that fails, the sound is destroyed (and so stopped) on the way out.
	try
	{
		newSound.lpSoundBuffer = lpSoundBuffer;
		m_playingSoundMap.emplace(hSFile, std::move(newSound));
	}
	catch (...)
	{ return FALSE; }

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

	// Destroy the sound, then close the file if the game already tried to. The sound has to go first, as it reads from the file.
	BOOL bCloseOnStop = sound.bCloseOnStop;
	sound.lpSound.reset();
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

	// If the sound has finished, return false to communicate this
	IAudioSound &sound = *itSound->second.lpSound;
	uint64_t nCurPos, nLength;
	if (!sound.IsPlaying() || !sound.GetCursor(&nCurPos))
		return FALSE;

	// For some files the length is only approximate or not known at all. That means it's possible that the current position >= the length. Need to handle this case to make sure the position is always < the length in the values we return.
	if (!sound.GetLength(&nLength) || nCurPos >= nLength)
		nLength = nCurPos + 1;

	uint64_t nCurPosBytes = DdaFramesToBytes(nCurPos, sound.GetChannels()),
		nLengthBytes = DdaFramesToBytes(nLength, sound.GetChannels());

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
		sound.lpSound->SetVolume(DdaVolumeToLinear(nVolume));
		if (sound.lpSoundBuffer)
			sound.lpSoundBuffer->SetVolume(nVolume);
	}
	if (nPanPos != SFILE_DDA_DEFAULT)
	{
		sound.nPanPos = nPanPos;
		sound.lpSound->SetPan(DdaPanToLinear(nPanPos));
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

void CDdaPlayer::UpdateSound(IN PLAYINGSOUND &sound)
{
	if (!sound.bIsPlaying)
		return;

	if (!sound.lpSound->IsPlaying())
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

			sound.lpSound->SetVolume(DdaVolumeToLinear(nVolume));
			sound.nVolume = nVolume;
		}
		if (SUCCEEDED(sound.lpSoundBuffer->GetPan(&nPanPos)) &&
			nPanPos != sound.nPanPos)
		{
			QDebugWriteEntry("CDdaPlayer::UpdateSound : Updating panning from %ld to %ld", sound.nPanPos, nPanPos);

			sound.lpSound->SetPan(DdaPanToLinear(nPanPos));
			sound.nPanPos = nPanPos;
		}
		if (SUCCEEDED(sound.lpSoundBuffer->GetFrequency(&nFrequency)) &&
			nFrequency != sound.nFrequency)
		{
			QDebugWriteEntry("CDdaPlayer::UpdateSound : Updating frequency from %lu to %lu", sound.nFrequency, nFrequency);

			sound.lpSound->SetPitch(DdaPitchRatio(nFrequency, sound.nBufferFrequency));
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

	for (PlayingSoundMap::iterator itSound = m_playingSoundMap.begin(); itSound != m_playingSoundMap.end(); itSound++)
	{
		PLAYINGSOUND &sound = itSound->second;

		UpdateSound(sound);

		// Mute/unmute non-global sounds
		if (bActivationChanged && !sound.bGlobal)
			sound.lpSound->SetMuted(!bActivated);
	}

	m_bActivated = bActivated;
}
