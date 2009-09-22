/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2009 Justin Olbrantz. All Rights Reserved.
*/

#include "stdafx.h"
#include "ThunderGraft.h"

using namespace FMOD;

// GUIDs of interfaces we'll need to get with QueryInterface at some point
const GUID IID_IDirectSound = { 0x279AFA83, 0x4981, 0x11CE, { 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60 } };
const GUID IID_IDirectSound8 = { 0xC50A7E93, 0xF395, 0x4834, { 0x9E, 0xF6, 0x7F, 0xA9, 0x9D, 0xE5, 0x09, 0x66 } };
const GUID IID_IDirectSoundBuffer = { 0x279AFA85, 0x4981, 0x11CE, { 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60 } };
const GUID IID_IDirectSoundBuffer8 = { 0x6825a449, 0x7524, 0x4d82, { 0x92, 0x0f, 0x50, 0xe3, 0x6a, 0xb3, 0xab, 0x1e } };

CThunderGraftPlugin ThunderGraft;

// The only reason for this is that we need to save our module handle for later use
BOOL APIENTRY DllMain( HMODULE hModule, 
                       DWORD  ul_reason_for_call, 
                       LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
			ThunderGraft.SetThisModule(hModule);

			break;
	}

    return TRUE;
}

CThunderGraftPlugin::CThunderGraftPlugin()
{
	//QDebugWriteEntry("CThunderGraftPlugin()");

	m_bInitialized = FALSE;
	m_bShutDown = FALSE;

	m_hUpdateThread = NULL;
	m_hKillUpdateThread = NULL;

	m_hThunderGraft = NULL;

	m_bSSCreated = FALSE;
	m_lpSoundSystem = NULL;
	m_lpDirectSound = NULL;

	LoadLibraryA = NULL;
	GetProcAddress = NULL;

	SFileGetFileSize = NULL;
	SFileReadFile = NULL;
	SFileSetFilePointer = NULL;

	SFileDdaBegin = NULL;
	SFileDdaBeginEx = NULL;
	SFileDdaDestroy = NULL;
	SFileDdaGetPos = NULL;
	SFileDdaGetPos = NULL;
	SFileDdaGetVolume = NULL;
	SFileDdaInitialize = NULL;
	SFileDdaSetVolume = NULL;

	DirectSoundCreate = NULL;
	DirectSoundCreate8 = NULL;

	InitializeCriticalSection(&m_csThreadLock);
}

CThunderGraftPlugin::~CThunderGraftPlugin()
{
	QDebugWriteEntry("~CThunderGraftPlugin()");

	EnterCriticalSection(&m_csThreadLock);

	// This is a very minimalistic destructor by necessity. We're inside DllMain, which drastically restricts what we can do (e.g. trying to free the FMOD sound system would result in deadlock when it waits for its worker thread to exit). Furthermore, we still have hooks in various functions that we can't really remove, so we can't just shut down everything here. We have no choice but to clean up what we can on IDirectSound::Release, and leave anything still alive at this point to be cleaned up by Windows when the process terminates.
	if (m_bSSCreated)
	{
		// Not good. The sound system shouldn't still exist by this point, but there's nothing we can do about it in DllMain.
		QDebugWriteEntry("~CThunderGraftPlugin : Sound system still exists!");

#if defined(_DEBUG)
		// This is technically more than we should be doing in DllMain - we shouldn't be calling other DLLs. We're doing this only so that we can log the number of references.
		DirectSoundReleasePtr DirectSoundRelease = (DirectSoundReleasePtr)GetPatchedFunction(m_lpDirectSound, IUNKNOWN_RELEASE_INDEX);

		m_lpDirectSound->AddRef();
		ULONG nRefCount = DirectSoundRelease(m_lpDirectSound);
		QDebugWriteEntry("~CThunderGraftPlugin : DirectSoundRelease returned %u", nRefCount);
#endif
	}

	m_bShutDown = TRUE;

	LeaveCriticalSection(&m_csThreadLock);
}

FARPROC CThunderGraftPlugin::GetPatchedFunction(IN LPVOID lpObject, IN DWORD iTableEntry)
{
	QDebugWriteEntry("GetPatchedFunction(0x%X, %u)", lpObject, iTableEntry);

	assert(lpObject);

	// Find the original function in the v-table for the object passed
	VTablePatches::iterator itFunction = m_vtablePatches.find(VTableEntry(*(LPVOID **)lpObject, iTableEntry));
	BOOL bFound = (itFunction != m_vtablePatches.end());
	QDebugWriteEntry("GetPatchedFunction : Found %d, function address 0x%X", bFound, (bFound ? itFunction->second : NULL));

	assert(bFound);

	return (FARPROC)itFunction->second;
}

void CThunderGraftPlugin::UpdateSound(IN PLAYINGSOUND &sound, IN BOOL bActivated)
{
	QDebugWriteEntry("UpdateSound(0x%X, %d)", &sound, bActivated);

	assert(m_bSSCreated);

	// Verify that the sound is still playing and hasn't been reused by another sound. Note that performing this check before setting all the stuff is actually a race condition, but there's really no way around it.
	bool bChannelPlaying = false;
	Channel *lpChannel = sound.lpChannel;
	Sound *lpSound;
	if (lpChannel->isPlaying(&bChannelPlaying) != FMOD_OK ||
		!bChannelPlaying ||
		lpChannel->getCurrentSound(&lpSound) != FMOD_OK || 
		lpSound != sound.lpSound)
		return;

	if (sound.lpSoundBuffer)
	{
		// Update the volume, panning, and frequency
		long nVolume, nPanPos;
		DWORD nFrequency;
		
		if (SUCCEEDED(sound.lpSoundBuffer->GetVolume(&nVolume)) &&
			nVolume != sound.nVolume)
		{
			QDebugWriteEntry("UpdateSound : Updating volume from %d to %d", sound.nVolume, nVolume);

			lpChannel->setVolume(DirectSoundToFMODVolume(nVolume));
			sound.nVolume = nVolume;
		}
		if (SUCCEEDED(sound.lpSoundBuffer->GetPan(&nPanPos)) &&
			nPanPos != sound.nPanPos)
		{
			QDebugWriteEntry("UpdateSound : Updating panning from %d to %d", sound.nPanPos, nPanPos);

			lpChannel->setPan(DirectSoundToFMODPan(nPanPos));
			sound.nPanPos = nPanPos;
		}
		if (SUCCEEDED(sound.lpSoundBuffer->GetFrequency(&nFrequency)) &&
			nFrequency != sound.nFrequency)
		{
			QDebugWriteEntry("UpdateSound : Updating frequency from %u to %u", sound.nFrequency, nFrequency);

			lpChannel->setFrequency(DirectSoundToFMODFrequency(nFrequency) * sound.nFreqMult);
			sound.nFrequency = nFrequency;
		}
	}

	// Mute/unmute non-global channels
	if (!sound.bGlobal && bActivated != m_bWasActivated)
		lpChannel->setMute(!bActivated);
}

BOOL CThunderGraftPlugin::CreateUpdateThread()
{
	QDebugWriteEntry("CreateUpdateThread()");

	assert(!m_hUpdateThread);
	assert(!m_hKillUpdateThread);

	m_hKillUpdateThread = CreateEvent(NULL, TRUE, FALSE, NULL);
	QDebugWriteEntry("CreateUpdateThread : CreateEvent returned 0x%X", m_hKillUpdateThread);
	if (!m_hKillUpdateThread)
		return FALSE;

	unsigned int nThreadID;
	m_hUpdateThread = (HANDLE)_beginthreadex(NULL, 0, (start_address)UpdateThread, this, 0, &nThreadID);
	QDebugWriteEntry("CreateUpdateThread : CreateThread returned 0x%X", m_hUpdateThread);
	if (m_hUpdateThread)
		return TRUE;

	CloseHandle(m_hKillUpdateThread);

	return FALSE;
}

DWORD WINAPI CThunderGraftPlugin::UpdateThread(IN CThunderGraftPlugin *pThis)
{
	QDebugWriteEntry("UpdateThread(0x%X)", pThis);

	assert(pThis);

	do
	{
		EnterCriticalSection(&pThis->m_csThreadLock);

		assert(pThis->m_bInitialized);

		// Determine whether we need to mute/unmute sounds. Polling like this may seem like a really crude method compared to something like window hooks to look for activate messages, but when you consider that doing so would intercept all messages, while this only runs once every so often, this way requires a lot less overhead.
		BOOL bActivated = pThis->m_bWasActivated;
		HWND hwndForeground = GetForegroundWindow();
		if (hwndForeground)
		{
			DWORD dwForeProcID;
			GetWindowThreadProcessId(hwndForeground, &dwForeProcID);
			bActivated = (dwForeProcID == GetCurrentProcessId());

			if (bActivated != pThis->m_bWasActivated)
				QDebugWriteEntry("UpdateThread : Activation status changed from %d to %d", pThis->m_bWasActivated, bActivated);
		}

		// Update the sounds
		PlayingSoundMap::iterator itUpdate = pThis->m_playingSoundMap.begin();
		while (itUpdate != pThis->m_playingSoundMap.end())
		{
			// Get the position of the next item in case the update function feels like deleting this entry. At least I'm hoping this works with a map (was originally a list).
			PlayingSoundMap::iterator itNextUpdate = itUpdate;
			itNextUpdate++;

			PLAYINGSOUND &update = itUpdate->second;

			pThis->UpdateSound(update, bActivated);

			itUpdate = itNextUpdate;
		}

		pThis->m_bWasActivated = bActivated;

		LeaveCriticalSection(&pThis->m_csThreadLock);
	} while (WaitForSingleObject(pThis->m_hKillUpdateThread, 50) == WAIT_TIMEOUT);

	QDebugWriteEntry("UpdateThread : Terminating update thread", pThis);

	return 0;
}

BOOL CThunderGraftPlugin::PlaySound(IN HANDLE hSFile, IN DWORD dwFlags, IN DWORD dwBufferBytes, OPTIONAL IN DWORD dwStartPosBytes, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos, OPTIONAL IN IDirectSoundBuffer *lpSoundBuffer)
{
	QDebugWriteEntry("PlaySound(0x%X, 0x%X, %u, %u, %d, %d, 0x%X)", hSFile, dwFlags, dwBufferBytes, dwStartPosBytes, nVolume, nPanPos, lpSoundBuffer);

	assert(m_bSSCreated);

	// If the sound is already playing, we need to stop it so we can restart it
	StopSound(hSFile);

	DWORD nFrequency = SFILE_DDA_DEFAULT, nBufferFreq = 0;

	// Set up the flags and creation parameters. Need to set up the buffer size, seek position, and the Storm I/O functions.
	// There is a bug/feature in FMOD that it will stop searching for valid data after 4 KB of file. If the ID3 or whatever tag is before the audio data and FMOD_IGNORETAGS is specified, this results in the file not being playable. FMOD_MPEGSEARCH is the supposed fix for this.
	FMOD_MODE mode = FMOD_IGNORETAGS | FMOD_MPEGSEARCH;
	if (dwFlags & SFILE_DDA_LOOPING)
		mode |= FMOD_LOOP_NORMAL;

	unsigned int nSampleBytes = 4, // Best guess: 16-bit stereo
		nBufferSamples = dwBufferBytes / nSampleBytes;
	BOOL bGlobal = FALSE;

	// If an IDirectSoundBuffer is provided, Storm uses that instead of creating its own buffer. While we can't use the buffer we get passed, we should adopt the buffer size and flags/caps from it. However, if a volume and pan position are also provided, Storm will update the buffer with those settings.
	if (lpSoundBuffer)
	{
		if (nVolume == SFILE_DDA_DEFAULT)
			lpSoundBuffer->GetVolume(&nVolume);
		else
			lpSoundBuffer->SetVolume(nVolume);

		if (nPanPos == SFILE_DDA_DEFAULT)
			lpSoundBuffer->GetPan(&nPanPos);
		else
			lpSoundBuffer->SetPan(nPanPos);

		if (nFrequency == SFILE_DDA_DEFAULT)
			lpSoundBuffer->GetFrequency(&nFrequency);
		else
			lpSoundBuffer->SetFrequency(nFrequency);

		// Get the buffer size, sampling rate, and caps
		DSBCAPS bufferCaps;
		WAVEFORMATEX bufferFormat;
		DWORD nBytesWritten;

		bufferCaps.dwSize = sizeof(bufferCaps);
		if (SUCCEEDED(lpSoundBuffer->GetCaps(&bufferCaps)) &&
			SUCCEEDED(lpSoundBuffer->GetFormat(&bufferFormat, sizeof(bufferFormat), &nBytesWritten)) &&
			nBytesWritten >= sizeof(bufferFormat))
		{
			bGlobal = (bufferCaps.dwFlags & DSBCAPS_GLOBALFOCUS);

			nSampleBytes = (bufferFormat.wBitsPerSample / 8) * bufferFormat.nChannels;
			nBufferSamples = bufferCaps.dwBufferBytes / nSampleBytes;
			nBufferFreq = bufferFormat.nSamplesPerSec;

			QDebugWriteEntry("PlaySound : IDirectSoundBuffer caps 0x%X, buffer size %u bytes, %u samples per second, %u bits per sample, %u channels", bufferCaps.dwFlags, bufferCaps.dwBufferBytes, bufferFormat.nSamplesPerSec, bufferFormat.wBitsPerSample, bufferFormat.nChannels);
		}
	}

	QDebugWriteEntry("PlaySound : Final volume %d, pan %d, frequency %u, size %u %u-byte samples, is %sglobal", nVolume, nPanPos, nFrequency, nBufferSamples, nSampleBytes, (bGlobal ? "" : "not "));

	FMOD_CREATESOUNDEXINFO soundParams;
	memset(&soundParams, 0, sizeof(soundParams));

	soundParams.cbsize = sizeof(soundParams);
	soundParams.decodebuffersize = nBufferSamples;
	soundParams.initialseekposition = dwStartPosBytes;
	soundParams.initialseekpostype = FMOD_TIMEUNIT_PCMBYTES;

	soundParams.useropen = FMODFileOpenCallback;
	soundParams.userclose = FMODFileCloseCallback;
	soundParams.userread = FMODFileReadCallback;
	soundParams.userseek = FMODFileSeekCallback;

	// Create the stream. Essentially this opens the file in preparation for playing.
	Sound *lpSound;
	FMOD_RESULT retVal = m_lpSoundSystem->createStream((char *)hSFile, mode, &soundParams, &lpSound);
	QDebugWriteEntry("PlaySound : createStream returned %d", retVal);
	if (retVal != FMOD_OK)
		return FALSE;

	// Set up the volume and other things, if necessary
	float nDefVolume, nDefFreq, nDefPan;
	int nDefPriority;
	lpSound->getDefaults(&nDefFreq, &nDefVolume, &nDefPan, &nDefPriority);

	float nFreqMult = 1;
	if (lpSoundBuffer)
		nFreqMult = (float)nDefFreq / (float)nBufferFreq;

	if (nVolume != SFILE_DDA_DEFAULT)
		nDefVolume = DirectSoundToFMODVolume(nVolume);
	if (nPanPos != SFILE_DDA_DEFAULT)
		nDefPan = DirectSoundToFMODPan(nPanPos);
	if (nFrequency != SFILE_DDA_DEFAULT)
		nDefFreq = DirectSoundToFMODFrequency(nFrequency) * nFreqMult;

	retVal = lpSound->setDefaults(nDefFreq, nDefVolume, nDefPan, nDefPriority);
	QDebugWriteEntry("PlaySound : setDefaults returned %d", retVal);

	// Play the sound
	Channel *lpChannel;
	retVal = m_lpSoundSystem->playSound(FMOD_CHANNEL_FREE, lpSound, false, &lpChannel);
	QDebugWriteEntry("PlaySound : playSound returned %d", retVal);
	if (retVal == FMOD_OK)
	{
		// The sound is playing fine. Add it to the lists and we're done.
		try
		{
			PLAYINGSOUND newSound(lpSound, lpChannel, bGlobal);

			newSound.lpSoundBuffer = lpSoundBuffer;

			newSound.nVolume = nVolume;
			newSound.nPanPos = nPanPos;
			newSound.nFrequency = nFrequency;

			newSound.nFreqMult = nFreqMult;

			pair <PlayingSoundMap::iterator, bool> insertResult = m_playingSoundMap.insert(PlayingSoundMap::value_type(hSFile, newSound));

			if (lpSoundBuffer)
			{
				// Done here for one less thing to roll back if the push fails
				ULONG nRefCount = lpSoundBuffer->AddRef();
				QDebugWriteEntry("PlaySound : Buffer AddRef returned %u", nRefCount);
			}

			return TRUE;
		}
		catch (...)
		{ }

		// Channel will be stopped when the sound is released
	}

	lpSound->release();

	return FALSE;
}

BOOL CThunderGraftPlugin::StopSound(IN HANDLE hSFile)
{
	QDebugWriteEntry("StopSound(0x%X)", hSFile);

	assert(m_bSSCreated);

	// See if the sound is playing
	PlayingSoundMap::iterator itSound = m_playingSoundMap.find(hSFile);
	if (itSound == m_playingSoundMap.end())
		return FALSE;

	// If necessary, release our pointer to the IDirectSoundBuffer
	PLAYINGSOUND *lpSound = &itSound->second;
	if (lpSound->lpSoundBuffer)
	{
		ULONG nRefCount = lpSound->lpSoundBuffer->Release();
		QDebugWriteEntry("StopSound : Buffer Release returned %u", nRefCount);
	}

	// Destroy the sound and close the file, if necessary
	lpSound->lpSound->release();
	if (lpSound->bCloseOnStop)
		SFileCloseFile(itSound->first);
	m_playingSoundMap.erase(itSound);

	return TRUE;
}

void CThunderGraftPlugin::StopAllSounds()
{
	QDebugWriteEntry("StopAllSounds()");

	assert(m_bSSCreated);

	while (m_playingSoundMap.size() > 0)
	{
		BOOL bRetVal = StopSound(m_playingSoundMap.begin()->first);
		QDebugWriteEntry("StopAllSounds : StopSound returned %d", bRetVal);
	}
}

BOOL CThunderGraftPlugin::GetFunctionPointers()
{
	QDebugWriteEntry("GetFunctionPointers()");

	assert(!m_bInitialized);

	BOOL bRetVal;

	m_hHostModule = GetModuleHandle(NULL);
	assert(m_hHostModule);

	// Get the Kernel functions
	m_hKernel32 = GetModuleHandle("Kernel32");
	QDebugWriteEntry("GetFunctionPointers : Kernel32.dll found at 0x%X", m_hKernel32);

	// We have to get these here because we need to use them ourselves. As a result, we can't wait until PatchFunctions.
	bRetVal = PatchImportEntry(m_hHostModule, m_hKernel32, "GetProcAddress", 0, NULL, &GetProcAddress);
	QDebugWriteEntry("GetFunctionPointers : PatchImportEntry returned %d", bRetVal);
	bRetVal = PatchImportEntry(m_hHostModule, m_hKernel32, "LoadLibraryA", 0, NULL, &LoadLibraryA);
	QDebugWriteEntry("GetFunctionPointers : PatchImportEntry returned %d", bRetVal);

	// Get the DirectSound functions
	m_hDirectSound = GetModuleHandle("Dsound");
	// It may or may not be loaded at load-time, so it might not be there at this point. Load it if necessary.
	if (!m_hDirectSound)
		m_hDirectSound = LoadLibrary("Dsound");
	QDebugWriteEntry("GetFunctionPointers : Dsound.dll found at 0x%X", m_hDirectSound);
	if (!m_hDirectSound)
		return FALSE;

	// Get the Storm functions
	m_hStorm = GetModuleHandle("Storm");
	if (!m_hStorm)
		return FALSE;

	QDebugWriteEntry("GetFunctionPointers : Storm.dll found at 0x%X", m_hStorm);

	bRetVal = PatchImportEntry(m_hHostModule, m_hStorm, NULL, 0x109, NULL, &SFileGetFileSize);
	QDebugWriteEntry("GetFunctionPointers : PatchImportEntry returned %d", bRetVal);
	bRetVal = PatchImportEntry(m_hHostModule, m_hStorm, NULL, 0x10D, NULL, &SFileReadFile);
	QDebugWriteEntry("GetFunctionPointers : PatchImportEntry returned %d", bRetVal);
	bRetVal = PatchImportEntry(m_hHostModule, m_hStorm, NULL, 0x10F, NULL, &SFileSetFilePointer);
	QDebugWriteEntry("GetFunctionPointers : PatchImportEntry returned %d", bRetVal);

	// See if StarEdit is running
	m_hStarEdit = GetModuleHandle("StarEdit.exe");
	QDebugWriteEntry("GetFunctionPointers : StarEdit.exe found at 0x%X", m_hStarEdit);

	// Make sure we have everything we need
	if (!SFileGetFileSize || !SFileReadFile || !SFileSetFilePointer)
		return FALSE;

	return TRUE;
}

BOOL CThunderGraftPlugin::PatchFunctionsInModule(IN HMODULE hModule, IN LPCSTR lpszStormName)
{
	QDebugWriteEntry("PatchFunctionsInModule(0x%X, %s)", hModule, lpszStormName);

	assert(hModule);
	assert(m_hKernel32);
	assert(m_hDirectSound);
	assert(m_hStorm);

	BOOL bRetVal;

	// The patching method used in ThunderGraft is both more robust and a lot trickier to use than the method employed in MPQDraft. Because other plugins may have already intercepted the functions we need to hook, we can't simply search the import tables for the precise function address - we have to look for the function name or ordinal. This causes a couple of problem. One, we have to make sure we don't intercept the same function twice; as we don't need to do otherwise, we trivially accomplish this by simply only patching once, patching only the modules imported by the game executable. Second, if other modules have already intercepted our functions, we need to call those - not the original, unpatched versions - from our own hook functions; this means we need to figure out which version of each function to use, which is handled by CThunderGraftPlugin::PatchImportEntry.

	// Patch the Kernel32 functions
	/*bRetVal = PatchImportEntry(hModule, m_hKernel32, "LoadLibraryA", 0, PatchLoadLibraryA, NULL);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);*/
	bRetVal = PatchImportEntry(hModule, m_hKernel32, "GetProcAddress", 0, PatchGetProcAddress, NULL);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);

	// Patch the DirectSound functions
	// For some reason, Diablo II imports functions from several modules by ordinal, even though functions in those libraries are exported by name and ordinal values are not constant. Dsound.dll is one such library.
	bRetVal = PatchImportEntry(hModule, m_hDirectSound, "DirectSoundCreate", 1, PatchDirectSoundCreate, &DirectSoundCreate);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);
	if (DirectSoundCreate8)
	{
		bRetVal = PatchImportEntry(hModule, m_hDirectSound, "DirectSoundCreate8", 0, PatchDirectSoundCreate8, &DirectSoundCreate8);
		QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);
	}

	// Patch the Storm functions
	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, 0x104, PatchDdaInitialize, &SFileDdaInitialize);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);
	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, 0x100, PatchDdaDestroy, &SFileDdaDestroy);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);

	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, 0xFE, PatchDdaBegin, &SFileDdaBegin);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);
	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, 0xFF, PatchDdaBeginEx, &SFileDdaBeginEx);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);
	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, 0x101, PatchDdaEnd, &SFileDdaEnd);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);
	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, 0xFD, PatchCloseFile, &SFileCloseFile);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);

	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, 0x102, PatchDdaGetPos, &SFileDdaGetPos);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);
	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, 0x103, PatchDdaGetVolume, &SFileDdaGetVolume);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);
	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, 0x105, PatchDdaSetVolume, &SFileDdaSetVolume);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);

	// Make sure we have everything we need
	if (!DirectSoundCreate || !SFileCloseFile || 
		!SFileDdaBegin || !SFileDdaBeginEx || !SFileDdaDestroy || 
		!SFileDdaEnd || !SFileDdaGetPos || !SFileDdaGetVolume || 
		!SFileDdaInitialize || !SFileDdaSetVolume)
		return FALSE;

	return TRUE;
}

BOOL CThunderGraftPlugin::PatchImportEntry(IN HMODULE hHostProgram, IN HMODULE hExportModule, IN OPTIONAL LPCSTR lpszFunctionName, IN OPTIONAL DWORD nFunctionOrdinal, IN OPTIONAL LPVOID pfnNewFunction, OUT OPTIONAL LPVOID ppfnOldFunction)
{
	QDebugWriteEntry("PatchImportEntry(0x%X, 0x%X, %s, %u, 0x%X, 0x%X)", hHostProgram, hExportModule, (lpszFunctionName ? lpszFunctionName : "NULL"), nFunctionOrdinal, pfnNewFunction, ppfnOldFunction);

	// This is essentially a wrapper around the function by the same name in QHookAPI.cpp. In addition to doing the patch and/or getting the list of function addresses, this one searches through the list of function addresses to find the one we should adopt as out "unpatched" version (the version of the function we call from without our patched functions.
	// The reason this is necessary is that either the patcher (e.g. FireGraft) or another plugin may have already patched the functions we want before we got called. If we overwrite the previous patches, we may break the patcher or other plugins; if we do a trivial function address search for the target function, we may break if another plugin has already altered the function address. Thus we get to do things the hard way.

	// Patch the module(s)
	ModuleFunctionMap moduleMap;
	DWORD nRetVal = ::PatchImportEntry(hHostProgram, hExportModule, lpszFunctionName, nFunctionOrdinal, (FARPROC)pfnNewFunction, &moduleMap, TRUE);
	QDebugWriteEntry("PatchImportEntry : ::PatchImportEntry returned %u", nRetVal);
	if (nRetVal == (DWORD)-1)
		return FALSE;

	if (!ppfnOldFunction || *(FARPROC *)ppfnOldFunction)
		return TRUE;	// Don't care or we already have one

	// Determine which function we want to establish as the one we'll call from our hook function
	LPCSTR lpszFunction = (lpszFunctionName ? lpszFunctionName : (LPCSTR)nFunctionOrdinal);
	FARPROC lpfnOldFunction = NULL, lpfnProcAddr; 
	
	// Need to be careful here. We may be getting called to find GetProcAddress, so we can't just call it straight-off
	if (GetProcAddress)
		lpfnProcAddr = GetProcAddress(hExportModule, lpszFunction);
	else
		lpfnProcAddr = ::GetProcAddress(hExportModule, lpszFunction);

	if (moduleMap.size() > 0)
	{
		// Look for the best fit function
		// Since it's possible that GetProcAddress will give us a patched function, we need to look elsewhere to determine if an import entry points to the original function. Approximate solution: see if the address is inside the actual exporting module.
		MODULEINFO exportInfo;
		BOOL bRetVal = GetModuleInformation(GetCurrentProcess(), hExportModule, &exportInfo, sizeof(exportInfo));
		UINT_PTR nModuleBase = (UINT_PTR)exportInfo.lpBaseOfDll, nModuleEnd = nModuleBase + exportInfo.SizeOfImage;

		QDebugWriteEntry("PatchImportEntry : GetModuleInformation returned %d, module located at 0x%X-0x%X", bRetVal, nModuleBase, nModuleEnd);
		if (!bRetVal)
			return FALSE;

		HMODULE hHost = GetModuleHandle(NULL);
		for (ModuleFunctionMap::iterator itFunc = moduleMap.begin();
			itFunc != moduleMap.end(); itFunc++)
		{
			UINT_PTR nFuncAddr = (UINT_PTR)itFunc->second;
			if (nFuncAddr >= nModuleBase && nFuncAddr < nModuleEnd)
				continue;	// Not patched
			else if (itFunc->second == lpfnProcAddr)
				continue;	// Patched but there might be something better

			QDebugWriteEntry("PatchImportEntry : Module 0x%X has been patched to 0x%X", itFunc->first, itFunc->second);

			// It's been patched by someone else. Give it priority.
			lpfnOldFunction = itFunc->second;

			// Stop right now if it's also in the host module
			if (itFunc->first == hHost)
				break;
		}
	}

	// We don't have something better, so go with what GetProcAddress gave us
	if (!lpfnOldFunction)
		lpfnOldFunction = lpfnProcAddr;

	*(FARPROC *)ppfnOldFunction = lpfnOldFunction;

	return TRUE;
}

BOOL CThunderGraftPlugin::PatchObjectVtableEntry(IN LPVOID lpObject, IN DWORD iTableEntry, IN LPVOID lpfnNewFunction)
{
	QDebugWriteEntry("PatchObjectVtableEntry(0x%X, %u, 0x%X)", lpObject, iTableEntry, lpfnNewFunction);

	assert(lpObject);
	assert(lpfnNewFunction);

	// This is quite a bit easier than import table patching, as we don't need to go searching through the module for the pointer we're looking for.
	LPVOID *lpVTable = *(LPVOID **)lpObject, *lpFunction = lpVTable + iTableEntry, lpSavedPointer = NULL;

	VTableEntry vtablePair(lpVTable, iTableEntry);
	VTablePatches::iterator itFunction = m_vtablePatches.find(vtablePair);
	if (itFunction != m_vtablePatches.end())
		lpSavedPointer = itFunction->second;

	QDebugWriteEntry("PatchObjectVtableEntry : v-table at 0x%X, function %u at 0x%X, saved function pointer is 0x%X", lpVTable, iTableEntry, *lpFunction, lpSavedPointer);

	if (lpSavedPointer)
		return TRUE;	// Already patched

	try
	{
		VTablePatches::value_type value(vtablePair, *lpFunction);
		m_vtablePatches.insert(value);
	}
	catch (...)
	{ return FALSE; }

	DWORD dwOldProtect, dwCows;
	if (!VirtualProtect(lpFunction, sizeof(FARPROC), PAGE_EXECUTE_READWRITE, &dwOldProtect))
		return FALSE;

	*lpFunction = lpfnNewFunction;

	VirtualProtect(lpFunction, sizeof(FARPROC), dwOldProtect, &dwCows);

	return TRUE;
}

BOOL CThunderGraftPlugin::PatchIDirectSound(IN IDirectSound *lpDirectSound)
{
	QDebugWriteEntry("PatchIDirectSound(0x%X)", lpDirectSound);

	assert(lpDirectSound);

	BOOL bRetVal = FALSE;
	HRESULT hResult = E_UNEXPECTED;
	DWORD iFuncIndx = IUNKNOWN_RELEASE_INDEX;

	// Patch the IDirectSound
	IDirectSound *lpDirectSound1;
	hResult = lpDirectSound->QueryInterface(IID_IDirectSound, (void **)&lpDirectSound1);
	if (SUCCEEDED(hResult))
	{
		bRetVal = PatchObjectVtableEntry(lpDirectSound1, iFuncIndx, (FARPROC)PatchDirectSoundRelease);
		QDebugWriteEntry("PatchIDirectSound : PatchObjectVtableEntry returned %d", bRetVal);
		bRetVal = PatchObjectVtableEntry(lpDirectSound1, IDIRECTSOUND_CREATESOUNDBUFFER_INDEX, (FARPROC)PatchCreateSoundBuffer);
		QDebugWriteEntry("PatchIDirectSound : PatchObjectVtableEntry returned %d", bRetVal);

		DirectSoundReleasePtr DirectSoundRelease = (DirectSoundReleasePtr)GetPatchedFunction(lpDirectSound1, IUNKNOWN_RELEASE_INDEX);
		DirectSoundRelease(lpDirectSound1);
	}

	// Patch the IDirectSound8
	IDirectSound8 *lpDirectSound8;
	hResult = lpDirectSound->QueryInterface(IID_IDirectSound8, (void **)&lpDirectSound8);
	if (SUCCEEDED(hResult))
	{
		bRetVal = PatchObjectVtableEntry(lpDirectSound8, iFuncIndx, (FARPROC)PatchDirectSoundRelease);
		QDebugWriteEntry("PatchIDirectSound : PatchObjectVtableEntry returned %d", bRetVal);
		bRetVal = PatchObjectVtableEntry(lpDirectSound8, IDIRECTSOUND_CREATESOUNDBUFFER_INDEX, (FARPROC)PatchCreateSoundBuffer);
		QDebugWriteEntry("PatchIDirectSound : PatchObjectVtableEntry returned %d", bRetVal);

		DirectSoundReleasePtr DirectSoundRelease = (DirectSoundReleasePtr)GetPatchedFunction(lpDirectSound8, IUNKNOWN_RELEASE_INDEX);
		DirectSoundRelease(lpDirectSound8);
	}

	// Patch the IUnknown, for the really long shots
	IUnknown *lpUnknown;
	hResult = lpDirectSound->QueryInterface(IID_IUnknown, (void **)&lpUnknown);
	if (SUCCEEDED(hResult))
	{
		bRetVal = PatchObjectVtableEntry(lpUnknown, iFuncIndx, (FARPROC)PatchDirectSoundRelease);
		QDebugWriteEntry("PatchIDirectSound : PatchObjectVtableEntry returned %d", bRetVal);

		DirectSoundReleasePtr DirectSoundRelease = (DirectSoundReleasePtr)GetPatchedFunction(lpUnknown, IUNKNOWN_RELEASE_INDEX);
		DirectSoundRelease(lpUnknown);
	}

	return TRUE;
}

BOOL CThunderGraftPlugin::PatchIDirectSoundBuffer(IN IDirectSoundBuffer *lpBuffer)
{
	QDebugWriteEntry("PatchIDirectSoundBuffer(0x%X)", lpBuffer);

	assert(lpBuffer);

	BOOL bRetVal = FALSE;
	HRESULT hResult = E_UNEXPECTED;

	IDirectSoundBuffer *lpBuffer1;
	hResult = lpBuffer->QueryInterface(IID_IDirectSoundBuffer, (void **)&lpBuffer1);
	if (SUCCEEDED(hResult))
	{
		bRetVal = PatchObjectVtableEntry(lpBuffer1, IDIRECTSOUNDBUFFER_SETFORMAT_INDEX, (FARPROC)PatchBufferSetFormat);
		QDebugWriteEntry("PatchIDirectSoundBuffer : PatchObjectVtableEntry returned %d", bRetVal);

		lpBuffer1->Release();
	}

	IDirectSoundBuffer8 *lpBuffer8;
	hResult = lpBuffer->QueryInterface(IID_IDirectSoundBuffer8, (void **)&lpBuffer8);
	if (SUCCEEDED(hResult))
	{
		bRetVal = PatchObjectVtableEntry(lpBuffer8, IDIRECTSOUNDBUFFER_SETFORMAT_INDEX, (FARPROC)PatchBufferSetFormat);
		QDebugWriteEntry("PatchIDirectSoundBuffer : PatchObjectVtableEntry returned %d", bRetVal);

		lpBuffer8->Release();
	}

	return TRUE;
}

BOOL CThunderGraftPlugin::InitializeSoundSystem(OPTIONAL IN LPCGUID lpGUID)
{
	QDebugWriteEntry("InitializeSoundSystem(0x%X)", lpGUID);

	assert(!m_bSSCreated);

	// Create the sound system
	FMOD_RESULT result = System_Create(&m_lpSoundSystem);
	QDebugWriteEntry("InitializeSoundSystem : System_Create returned %d", result);
	if (result != FMOD_OK)
		return FALSE;

	// Configure and initialize it
	if (ConfigureSoundSystem(lpGUID))
	{
		// Get the IDirectSound interface from the sound system
		FMOD_RESULT result = ThunderGraft.m_lpSoundSystem->getOutputHandle((void **)&m_lpDirectSound);
		QDebugWriteEntry("InitializeSoundSystem : getOutputHandle returned %d, IDirectSound at 0x%X", result, m_lpDirectSound);
		if (result == FMOD_OK)
		{
			m_lpDirectSound->AddRef();
			m_bSSCreated = TRUE;

			m_bWasActivated = TRUE;

			return TRUE;
		}
	}

	// Failure
	m_lpSoundSystem->release();
	m_lpSoundSystem = NULL;

	return FALSE;
}

BOOL CThunderGraftPlugin::ConfigureSoundSystem(OPTIONAL IN LPCGUID lpGUID)
{
	QDebugWriteEntry("ConfigureSoundSystem(0x%X)", lpGUID);

	assert(!m_bSSCreated);

	FMOD_RESULT result;

	// Need to make sure the system is using DirectSound
	result = m_lpSoundSystem->setOutput(FMOD_OUTPUTTYPE_DSOUND);
	QDebugWriteEntry("ConfigureSoundSystem : setOutput returned %d", result);
	if (result != FMOD_OK)
		return FALSE;

	//m_lpSoundSystem->setFileSystem(FMODFileOpenCallback, FMODFileCloseCallback, FMODFileReadCallback, FMODFileSeekCallback, 4096);

	// Determine which driver we want to use
	int nDefDriver = SetDriverFromGUID(lpGUID);
	QDebugWriteEntry("ConfigureSoundSystem : SetDriverFromGUID returned %d", nDefDriver);
	if (nDefDriver == -1)
		return FALSE;

	// Do all the stuff FMOD recommends
	// Set the default speaker mode for the driver
	FMOD_CAPS driverCaps;
	FMOD_SPEAKERMODE speakerMode;
	result = m_lpSoundSystem->getDriverCaps(nDefDriver, &driverCaps, 0, 0, &speakerMode);
	QDebugWriteEntry("ConfigureSoundSystem : getDriverCaps returned %d, caps %u, speaker mode %u", result, driverCaps, speakerMode);
	if (result != FMOD_OK)
		return FALSE;

	result = m_lpSoundSystem->setSpeakerMode(speakerMode);  /* Set the user selected speaker mode. */
	QDebugWriteEntry("SetSoundSystemOptions : setSpeakerMode returned %d", result);

	if (driverCaps & FMOD_CAPS_HARDWARE_EMULATED) /* The user has the 'Acceleration' slider set to off!  This is really bad for latency! */
	{
		result = m_lpSoundSystem->setDSPBufferSize(1024, 10);
		QDebugWriteEntry("ConfigureSoundSystem : No hardware acceleration. setDSPBufferSize returned %d", result);
	}

	// Check for buggy drivers
	char szDriverName[256] = "";
	result = m_lpSoundSystem->getDriverInfo(nDefDriver, szDriverName, sizeof(szDriverName), 0);
	QDebugWriteEntry("ConfigureSoundSystem : getDriverInfo returned %d, name '%s'", result, szDriverName);
	if (result != FMOD_OK)
		return FALSE;

	if (strstr(szDriverName, "SigmaTel"))   /* Sigmatel sound devices crackle for some reason if the format is PCM 16bit.  PCM floating int output seems to solve it. */
	{
		result = m_lpSoundSystem->setSoftwareFormat(48000, FMOD_SOUND_FORMAT_PCMFLOAT, 0,0, FMOD_DSP_RESAMPLER_LINEAR);
		QDebugWriteEntry("ConfigureSoundSystem : setSoftwareFormat returned %d", result);
	}

	// Finally, call init. Oh, and one more FMOD advised thing if init fails
	result = m_lpSoundSystem->init(MAX_SOUND_CHANNELS, 0, NULL);
	QDebugWriteEntry("ConfigureSoundSystem : init returned %d", result);
	if (result == FMOD_ERR_OUTPUT_CREATEBUFFER) /* Ok, the speaker mode selected isn't supported by this soundcard.  Swiit back to stereo... */
	{
		result = m_lpSoundSystem->setSpeakerMode(FMOD_SPEAKERMODE_STEREO);
		QDebugWriteEntry("ConfigureSoundSystem : setSpeakerMode returned %d", result);
		    
		result = m_lpSoundSystem->init(MAX_SOUND_CHANNELS, FMOD_INIT_NORMAL, 0); /* ... and re-init. */
		QDebugWriteEntry("ConfigureSoundSystem : init returned %d", result);
	}

	return (result == FMOD_OK);
}

int CThunderGraftPlugin::SetDriverFromGUID(OPTIONAL IN LPCGUID lpGUID)
{
	assert(!m_bSSCreated);

	// It really annoys me that I have to import the RPC library just to write the GUID to the log file
	RPC_CSTR lpszUUIDString;
	if (lpGUID && UuidToString((const UUID *)lpGUID, &lpszUUIDString) == RPC_S_OK)
	{
		QDebugWriteEntry("SetDriverFromGUID(%s)", lpszUUIDString);
		RpcStringFree(&lpszUUIDString);
	}
	else
		QDebugWriteEntry("SetDriverFromGUID(NULL)");

	FMOD_RESULT result;

	// Do they want the default driver?
	const GUID DSDEVID_DefaultPlayback = { 0xdef00000, 0x9c6d, 0x47ed, { 0xaa, 0xf1, 0x4d, 0xda, 0x8f, 0x2b, 0x5c, 0x03 } };
	if (!lpGUID || memcmp(lpGUID, &DSDEVID_DefaultPlayback, sizeof(GUID)) == 0)
	{
		// Get the default driver
		int nDefDriver;
		result = m_lpSoundSystem->getDriver(&nDefDriver);
		QDebugWriteEntry("SetDriverFromGUID : getDriver returned %d, driver %d", result, nDefDriver);
		if (result != FMOD_OK)
			return -1;

		return nDefDriver;
	}

	// Scan through the drivers, looking for the one with the GUID we were given
	int nDrivers;
	result = m_lpSoundSystem->getNumDrivers(&nDrivers);
	QDebugWriteEntry("SetDriverFromGUID : getNumDrivers returned %d, %d drivers", result, nDrivers);
	if (result != FMOD_OK)
		return -1;

	for (int i = 0; i < nDrivers; i++)
	{
		// Is it the one we're looking for?
		FMOD_GUID driverGUID;
		result = m_lpSoundSystem->getDriverInfo(i, NULL, 0, &driverGUID);

		if (UuidToString((const UUID *)&driverGUID, &lpszUUIDString) == RPC_S_OK)
		{
			QDebugWriteEntry("SetDriverFromGUID : Driver index %d, GUID %s", i, lpszUUIDString);
			RpcStringFree(&lpszUUIDString);
		}

		if (memcmp(lpGUID, &driverGUID, sizeof(driverGUID)) == 0)
			return i;	// Looks like it
	}

	return -1;	// Not found
}

BOOL CThunderGraftPlugin::DirectSoundCreateCommon(OPTIONAL IN LPCGUID lpGUID)
{
	QDebugWriteEntry("DirectSoundCreateCommon(0x%X)", lpGUID);

	assert(m_bInitialized);

	BOOL bRetVal;
	if (m_bSSCreated)
	{
		assert(m_lpDirectSound);

		m_lpDirectSound->AddRef();
		DirectSoundReleasePtr DirectSoundRelease = (DirectSoundReleasePtr)GetPatchedFunction(m_lpDirectSound, IUNKNOWN_RELEASE_INDEX);
		ULONG nRefCount = DirectSoundRelease(m_lpDirectSound);
		QDebugWriteEntry("DirectSoundCreateCommon : Still %u references on the IDirectSound!", nRefCount);

		// If we've ended up here, that means that the previous sound system wasn't freed by this point. That can only mean two things: either the game is leaking references to the IDirectSound interface, or FMOD is holding multiple references. Destroy the sound system here and hope that'll get us to 0.
		DestroySoundSystem();
	}

	bRetVal = InitializeSoundSystem(lpGUID);
	QDebugWriteEntry("DirectSoundCreateCommon : InitializeSoundSystem returned %d", bRetVal);
	if (!bRetVal)
		return FALSE;

	bRetVal = PatchIDirectSound(m_lpDirectSound);
	QDebugWriteEntry("DirectSoundCreateCommon : PatchIDirectSound returned %d", bRetVal);
	if (bRetVal)
		return TRUE;

	// Failure. Clean up.
	DestroySoundSystem();

	return FALSE;
}

void CThunderGraftPlugin::DestroySoundSystem()
{
	QDebugWriteEntry("DestroySoundSystem()");

	assert(m_bSSCreated);

	StopAllSounds();

	m_lpSoundSystem->release();
	m_lpSoundSystem = NULL;

	DirectSoundReleasePtr DirectSoundRelease = (DirectSoundReleasePtr)GetPatchedFunction(m_lpDirectSound, IUNKNOWN_RELEASE_INDEX);
	ULONG nRefCount = DirectSoundRelease(m_lpDirectSound);
	m_lpDirectSound = NULL;
	QDebugWriteEntry("DestroySoundSystem : IDirectSound::Release returned %u%s", nRefCount, (nRefCount > 0 ? "!" : ""));

	m_bSSCreated = FALSE;
}

void CThunderGraftPlugin::ShutDown()
{
	QDebugWriteEntry("ShutDown()");

	assert(m_bInitialized);

	SetEvent(m_hKillUpdateThread);
	CloseHandle(m_hUpdateThread);

	if (m_bSSCreated)
		DestroySoundSystem();

	m_bInitialized = FALSE;
}

FARPROC WINAPI CThunderGraftPlugin::DelayLoadHook(IN unsigned int dliNotify, IN PDelayLoadInfo pdli)
{
	QDebugWriteEntry("DelayLoadHook(%u, %s)", dliNotify, pdli->szDll);

	assert(pdli);

	if (dliNotify != dliNotePreLoadLibrary)
		return NULL;	// We don't care

	if (_stricmp(pdli->szDll, "fmodex.dll") != 0)
		return NULL;	// Not ours

	// It's ours. Give it the handle of the DLL, wherever it is.
	assert(ThunderGraft.m_hFMOD);

	// A cast to a function pointer. Sickening.
	return (FARPROC)ThunderGraft.m_hFMOD;
}

FMOD_RESULT F_CALLBACK CThunderGraftPlugin::FMODFileOpenCallback(
	const char *  name, 
	int  unicode, 
	unsigned int *  filesize, 
	void **  handle, 
	void **  userdata
)
{
	QDebugWriteEntry("FMODFileOpenCallback(0x%X, %d, 0x%X, 0x%X, 0x%X)", name, unicode, filesize, handle, userdata);

	assert(name);
	assert(handle);

	// Very little to do here; we're getting passed the HANDLE, so all we need to do is return the file size
	HANDLE hSFile = (HANDLE)name;
	if (filesize)
		*filesize = ThunderGraft.SFileGetFileSize(hSFile, NULL);

	*handle = (void *)hSFile;
	return FMOD_OK;
}

FMOD_RESULT F_CALLBACK CThunderGraftPlugin::FMODFileCloseCallback(
	void *  handle, 
	void *  userdata
)
{
	QDebugWriteEntry("FMODFileCloseCallback(0x%X)", handle);

	assert(handle);

	// Nothing to do here
	return FMOD_OK;
}

FMOD_RESULT F_CALLBACK CThunderGraftPlugin::FMODFileReadCallback(
	void *  handle, 
	void *  buffer, 
	unsigned int  sizebytes, 
	unsigned int *  bytesread, 
	void *  userdata
)
{
	// Do the read. FMOD requires a few specific implementations details. This functiom must return EOF if the bytes read is less than the bytes requested, in addition to no bytes being read because we're past the end of the file.
	QDebugWriteEntry("FMODFileReadCallback(0x%X, 0x%X, %u, x, 0x%X)", handle, buffer, sizebytes, userdata);

	assert(handle);
	assert(buffer);

	FMOD_RESULT retVal = FMOD_ERR_FILE_DISKEJECTED;	// Some random error value
	HANDLE hSFile = (HANDLE)handle;
	BOOL bSuccess = ThunderGraft.SFileReadFile(hSFile, buffer, sizebytes, (LPDWORD)bytesread, NULL);
	if (bSuccess)
		retVal = (*bytesread ==  sizebytes) ? FMOD_OK : FMOD_ERR_FILE_EOF;
	else if (GetLastError() == ERROR_HANDLE_EOF)
	{
		if (bytesread)
			*bytesread = 0;
		retVal = FMOD_ERR_FILE_EOF;
	}

	QDebugWriteEntry("FMODFileReadCallback : SFileReadFile returned %d, %u bytes read, FMOD_RESULT of %d", bSuccess, *bytesread, retVal);

	return retVal;
}

FMOD_RESULT F_CALLBACK CThunderGraftPlugin::FMODFileSeekCallback(
	void *  handle, 
	unsigned int  pos, 
	void *  userdata
)
{
	// Not real sure about this one. FMOD doesn't say exactly how the implementation is supposed to work, so we just set the position and leave the read callback to handle EOF.
	QDebugWriteEntry("FMODFileSeekCallback(0x%X, %u, 0x%X)", handle, pos, userdata);

	HANDLE hSFile = (HANDLE)handle;
	ThunderGraft.SFileSetFilePointer(hSFile, (LONG)pos, NULL, FILE_BEGIN);

	return FMOD_OK;
}

HMODULE WINAPI CThunderGraftPlugin::PatchLoadLibraryA(IN LPCSTR lpFileName)
{
	// Do not execute any custom code if we're supposed to be shut down
	if (ThunderGraft.m_bShutDown)
		return ThunderGraft.LoadLibrary(lpFileName);

	QDebugWriteEntry("PatchLoadLibraryA(%s)", lpFileName);

	if (!lpFileName)
		return NULL;

	// Not much to do here, just load the library and patch its functions so our overrides will be used
	HMODULE hLibrary = ThunderGraft.LoadLibrary(lpFileName);
	QDebugWriteEntry("PatchLoadLibraryA : LoadLibrary returned 0x%X", hLibrary);
	if (!hLibrary)
		return NULL;

	ThunderGraft.PatchFunctionsInModule(hLibrary, "Storm.dll");

	return hLibrary;
}

FARPROC WINAPI CThunderGraftPlugin::PatchGetProcAddress(OPTIONAL IN HMODULE hModule, IN LPCSTR lpProcName)
{
	// Do not execute any custom code if we're supposed to be shut down
	if (ThunderGraft.m_bShutDown)
		return ThunderGraft.GetProcAddress(hModule, lpProcName);

	// Need to override everything we patched the import tables of, here
	BOOL bNameIsOrd = (UINT_PTR)lpProcName < (64 << 10);	// Ordinals are 16-bits

	if (!bNameIsOrd)
		QDebugWriteEntry("PatchGetProcAddress(0x%X, %s)", hModule, lpProcName);
	else
		QDebugWriteEntry("PatchGetProcAddress(0x%X, 0x%X)", hModule, lpProcName);

	if (hModule == ThunderGraft.m_hStorm && bNameIsOrd)
	{
		QDebugWriteEntry("PatchGetProcAddress : Storm function");

		switch ((DWORD)lpProcName)
		{
			case 0xFD:
				return (FARPROC)ThunderGraft.PatchCloseFile;
			case 0xFE:
				return (FARPROC)ThunderGraft.PatchDdaBegin;
			case 0xFF:
				return (FARPROC)ThunderGraft.PatchDdaBeginEx;
			case 0x100:
				return (FARPROC)ThunderGraft.PatchDdaDestroy;
			case 0x101:
				return (FARPROC)ThunderGraft.PatchDdaEnd;
			case 0x102:
				return (FARPROC)ThunderGraft.PatchDdaGetPos;
			case 0x103:
				return (FARPROC)ThunderGraft.PatchDdaGetVolume;
			case 0x104:
				return (FARPROC)ThunderGraft.PatchDdaInitialize;
			case 0x105:
				return (FARPROC)ThunderGraft.PatchDdaSetVolume;
		}
	}
	else if (hModule == ThunderGraft.m_hKernel32 && !bNameIsOrd)
	{
		QDebugWriteEntry("PatchGetProcAddress : Kernel32 function");

		/*if (strcmp(lpProcName, "LoadLibraryA") == 0)
			return (FARPROC)ThunderGraft.PatchLoadLibraryA;
		else*/ if (strcmp(lpProcName, "GetProcAddress") == 0)
			return (FARPROC)ThunderGraft.PatchGetProcAddress;
	}
	else if (ThunderGraft.m_hDirectSound && hModule == ThunderGraft.m_hDirectSound)
	{
		if ((!bNameIsOrd && strcmp(lpProcName, "DirectSoundCreate") == 0) ||
			(bNameIsOrd && (DWORD)lpProcName == 1))
			return (FARPROC)ThunderGraft.PatchDirectSoundCreate;
		else if (!bNameIsOrd && strcmp(lpProcName, "DirectSoundCreate8") == 0)
			return (FARPROC)ThunderGraft.PatchDirectSoundCreate8;
	}

	FARPROC pfnRetVal = ThunderGraft.GetProcAddress(hModule, lpProcName);
	QDebugWriteEntry("PatchGetProcAddress : GetProcAddress returned 0x%X", pfnRetVal);

	return pfnRetVal;
}

BOOL WINAPI CThunderGraftPlugin::PatchDdaInitialize(IN IDirectSound *lpDirectSound)
{
	QDebugWriteEntry("PatchDdaInitialize(0x%X)", lpDirectSound);

	if (!lpDirectSound)
		return FALSE;

	// There really isn't that much to do here, in this version. The FMOD System must be created before the IDirectSound interface is generated, so that has to be done at the latest in DirectSoundCreate. Because of that, we don't have much to do here.
	BOOL bRetVal = FALSE;

	EnterCriticalSection(&ThunderGraft.m_csThreadLock);

	assert(ThunderGraft.m_bSSCreated);
	assert(lpDirectSound == ThunderGraft.m_lpDirectSound);

	// TODO: Decide whether to call SFileDdaInitialize/Destroy
	bRetVal = ThunderGraft.SFileDdaInitialize(lpDirectSound);
	QDebugWriteEntry("PatchDdaInitialize : SFileDdaInitialize returned %d", bRetVal);

	LeaveCriticalSection(&ThunderGraft.m_csThreadLock);

	return bRetVal;
}

BOOL WINAPI CThunderGraftPlugin::PatchCloseFile(IN HANDLE hSFile)
{
	//QDebugWriteEntry("PatchCloseFile(0x%X)", hSFile);	// Noisy

	if (!hSFile)
		return FALSE;

	// If the file is currently being played, need to delay closing the file until the sound is stopped
	EnterCriticalSection(&ThunderGraft.m_csThreadLock);

	assert(ThunderGraft.m_bInitialized);

	// If it's a playing sound, set it to close on stop. If not, close it now.
	PlayingSoundMap::iterator itSound = ThunderGraft.m_playingSoundMap.find(hSFile);
	BOOL bIsPlaying = (itSound != ThunderGraft.m_playingSoundMap.end());
	//QDebugWriteEntry("PatchCloseFile(0x%X) : Sound is %scurrently playing", hSFile, (bIsPlaying ? "" : "not "));	// Noisy

	if (bIsPlaying)
		itSound->second.bCloseOnStop = TRUE;

	LeaveCriticalSection(&ThunderGraft.m_csThreadLock);

	if (!bIsPlaying)
		return ThunderGraft.SFileCloseFile(hSFile);
	else
		return TRUE;
}

BOOL WINAPI CThunderGraftPlugin::PatchDdaBegin(IN HANDLE hSFile, IN DWORD dwWAVBufferSize, IN DWORD dwFlags)
{
	// Stub function
	QDebugWriteEntry("PatchDdaBegin(0x%X, %u, 0x%X)", hSFile, dwWAVBufferSize, dwFlags);

	return ThunderGraft.PatchDdaBeginEx(hSFile, dwWAVBufferSize, dwFlags, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, NULL);
}

// dwWAVBufferSize and dwWAVReadPtr are in bytes
BOOL WINAPI CThunderGraftPlugin::PatchDdaBeginEx(IN HANDLE hSFile, IN DWORD dwWAVBufferSize, IN DWORD dwFlags, OPTIONAL IN DWORD dwWAVReadPtr, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos, OPTIONAL IN IDirectSoundBuffer *lpSoundBuffer)
{
	QDebugWriteEntry("PatchDdaBeginEx(0x%X, %u, 0x%X, %u, %d, %d, 0x%X)", hSFile, dwWAVBufferSize, dwFlags, dwWAVReadPtr, nVolume, nPanPos, lpSoundBuffer);

	if (!hSFile || !dwWAVBufferSize)
		return FALSE;

	EnterCriticalSection(&ThunderGraft.m_csThreadLock);

	BOOL bRetVal = ThunderGraft.PlaySound(hSFile, dwFlags, dwWAVBufferSize, dwWAVReadPtr, nVolume, nPanPos, lpSoundBuffer);
	QDebugWriteEntry("PatchDdaBeginEx : PlaySound returned %d", bRetVal);

	LeaveCriticalSection(&ThunderGraft.m_csThreadLock);

	return bRetVal;
}

BOOL WINAPI CThunderGraftPlugin::PatchDdaEnd(IN HANDLE hSFile)
{
	QDebugWriteEntry("PatchDdaEnd(0x%X)", hSFile);

	if (!hSFile)
		return FALSE;

	EnterCriticalSection(&ThunderGraft.m_csThreadLock);

	BOOL bRetVal = ThunderGraft.StopSound(hSFile);
	QDebugWriteEntry("PatchDdaEnd : StopSound return %d", bRetVal);

	LeaveCriticalSection(&ThunderGraft.m_csThreadLock);

	return bRetVal;
}

BOOL WINAPI CThunderGraftPlugin::PatchDdaDestroy()
{
	QDebugWriteEntry("PatchDdaDestroy()");

	EnterCriticalSection(&ThunderGraft.m_csThreadLock);

	BOOL bRetVal = ThunderGraft.SFileDdaDestroy();
	QDebugWriteEntry("PatchDdaDestroy : SFileDdaDestroy returned %d", bRetVal);

	LeaveCriticalSection(&ThunderGraft.m_csThreadLock);

	return TRUE;
}

BOOL WINAPI CThunderGraftPlugin::PatchDdaGetPos(IN HANDLE hSFile, OPTIONAL OUT LPDWORD lpdwCurPos, OPTIONAL OUT LPDWORD lpdwLength)
{
	QDebugWriteEntry("PatchDdaGetPos(0x%X, x, x)", hSFile);

	if (!hSFile)
		return FALSE;

	EnterCriticalSection(&ThunderGraft.m_csThreadLock);

	BOOL bRetVal = FALSE;

	// There are a few different possibilities here. It could be a file that wasn't ever played, it could be playing right now, or it could have started but finished.
	PlayingSoundMap::iterator itSound = ThunderGraft.m_playingSoundMap.find(hSFile);
	QDebugWriteEntry("PatchDdaGetPos : Sound %sfound in list", 
		(itSound != ThunderGraft.m_playingSoundMap.end() ? "" : "not "));
	if (itSound != ThunderGraft.m_playingSoundMap.end())
	{
		// It was started
		unsigned int nCurPos, nLength;
		Sound *lpPlayingSound;
		
		if (itSound->second.lpSound->getLength(&nLength, FMOD_TIMEUNIT_PCMBYTES) == FMOD_OK)
		{
			// FMOD channels are reused. In addition to getting the position and length, we also need to make sure this channel is still playing this sound (stream sounds can only be played once, so we know it's the instance we want). Slight race condition here, but there's nothing we can do about that.
			bool bChannelPlaying = false;
			BOOL bStillPlaying = (itSound->second.lpChannel->getPosition(&nCurPos, FMOD_TIMEUNIT_PCMBYTES) == FMOD_OK &&
				itSound->second.lpChannel->isPlaying(&bChannelPlaying) == FMOD_OK &&
				bChannelPlaying &&
				itSound->second.lpChannel->getCurrentSound(&lpPlayingSound) == FMOD_OK &&
				lpPlayingSound == itSound->second.lpSound);
			QDebugWriteEntry("PatchDdaGetPos : Gets returned %d, at %u/%u", bStillPlaying, nCurPos, nLength);
			if (bStillPlaying)
			{

				// This is the right sound. Now there's just one more kink to work out. For some files (e.g. VBR MP3s) getLength is only approximate, not exact. That means it's possible that the current position > the value getLength returns. Need to handle this case to make sure the position is never > the length in the values we return.
				if (nCurPos >= nLength)
					nLength = nCurPos + 1;

				if (lpdwCurPos)
					*lpdwCurPos = nCurPos;
				if (lpdwLength)
					*lpdwLength = nLength;

				bRetVal = TRUE;
			}
			else
				// The sound has probably finished. Return false to communicate this.
				bRetVal = FALSE;
		}
	}

	LeaveCriticalSection(&ThunderGraft.m_csThreadLock);

	return bRetVal;
}

// Oddly enough, unlike SFileDdaGetPos, SFileDdaGetVolume and SFileDdaSetVolume do not fail when the sound has finished playing
BOOL WINAPI CThunderGraftPlugin::PatchDdaGetVolume(IN HANDLE hSFile, OPTIONAL OUT long *lpnVolume, OPTIONAL OUT long *lpnPanPos)
{
	QDebugWriteEntry("PatchDdaGetVolume(0x%X, 0x%X, 0x%X)", hSFile, lpnVolume, lpnPanPos);

	if (!hSFile)
		return FALSE;

	EnterCriticalSection(&ThunderGraft.m_csThreadLock);

	BOOL bRetVal = FALSE;

	// See if the sound has started
	PlayingSoundMap::iterator itSound = ThunderGraft.m_playingSoundMap.find(hSFile);
	QDebugWriteEntry("PatchDdaGetVolume : Sound %sfound in list", 
		(itSound != ThunderGraft.m_playingSoundMap.end() ? "" : "not "));
	if (itSound != ThunderGraft.m_playingSoundMap.end())
	{
		PLAYINGSOUND &sound = itSound->second;
		float nVolume, nPanPos;
		Sound *lpCurrentSound;

		// Is it still playing?
		// In theory this is susceptible to the same problem as SFileDdaGetPos: the sound may have ended. Watch for it.
		BOOL bStillPlaying = (sound.lpChannel->getVolume(&nVolume) == FMOD_OK &&
			sound.lpChannel->getPan(&nPanPos) == FMOD_OK &&
			sound.lpChannel->getCurrentSound(&lpCurrentSound) == FMOD_OK &&
			lpCurrentSound == sound.lpSound);
		QDebugWriteEntry("PatchDdaGetVolume : Gets returned %d, volume %d, pan %d", bStillPlaying, nVolume, nPanPos);
		if (bStillPlaying)
		{
			// It's still playing. Update the current values.
			itSound->second.nVolume = FMODToDirectSoundVolume(nVolume);
			itSound->second.nPanPos = FMODToDirectSoundPan(nPanPos);
		}

		if (lpnVolume)
			*lpnVolume = itSound->second.nVolume;
		if (lpnPanPos)
			*lpnPanPos = itSound->second.nPanPos;

		bRetVal = TRUE;
	}

	LeaveCriticalSection(&ThunderGraft.m_csThreadLock);

	return bRetVal;
}

BOOL WINAPI CThunderGraftPlugin::PatchDdaSetVolume(IN HANDLE hSFile, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos)
{
	QDebugWriteEntry("PatchDdaSetVolume(0x%X, %d, %d)", hSFile, nVolume, nPanPos);

	if (!hSFile)
		return FALSE;

	EnterCriticalSection(&ThunderGraft.m_csThreadLock);

	BOOL bRetVal = FALSE;

	PlayingSoundMap::iterator itSound = ThunderGraft.m_playingSoundMap.find(hSFile);
	QDebugWriteEntry("PatchDdaSetVolume : Sound %sfound in list", 
		(itSound != ThunderGraft.m_playingSoundMap.end() ? "" : "not "));
	if (itSound != ThunderGraft.m_playingSoundMap.end())
	{
		PLAYINGSOUND &sound = itSound->second;
		Sound *lpCurrentSound;

		if (nVolume != SFILE_DDA_DEFAULT)
			itSound->second.nVolume = nVolume;
		if (nPanPos != SFILE_DDA_DEFAULT)
			itSound->second.nPanPos = nPanPos;

		// In theory this is susceptible to the same problem as SFileDdaGetPos: the sound may have ended. Watch for it.
		BOOL bStillPlaying = (sound.lpChannel->getCurrentSound(&lpCurrentSound) == FMOD_OK &&
			lpCurrentSound == sound.lpSound);
		QDebugWriteEntry("PatchDdaSetVolume : Sound %sstill playing", (bStillPlaying ? "" : "not "));
		if (bStillPlaying)
		{
			if (nVolume != SFILE_DDA_DEFAULT)
				sound.lpChannel->setVolume(DirectSoundToFMODVolume(nVolume));
			if (nPanPos != SFILE_DDA_DEFAULT)
				sound.lpChannel->setPan(DirectSoundToFMODPan(nPanPos));

			bRetVal = TRUE;
		}
	}

	LeaveCriticalSection(&ThunderGraft.m_csThreadLock);

	return bRetVal;
}

HRESULT WINAPI CThunderGraftPlugin::PatchDirectSoundCreate(OPTIONAL IN LPCGUID lpcGuid, OUT LPDIRECTSOUND *ppDS, OPTIONAL IN LPUNKNOWN pUnkOuter)
{
	QDebugWriteEntry("PatchDirectSoundCreate(0x%X, x, x)", lpcGuid);

	if (!ppDS)
		return DSERR_INVALIDPARAM;
	if (pUnkOuter)
		return DSERR_NOAGGREGATION;

	EnterCriticalSection(&ThunderGraft.m_csThreadLock);

	BOOL bRetVal = ThunderGraft.DirectSoundCreateCommon(lpcGuid);
	QDebugWriteEntry("PatchDirectSoundCreate : DirectSoundCreateCommon returned %d", bRetVal);

	if (bRetVal)
	{
		*ppDS = ThunderGraft.m_lpDirectSound;
		ULONG nRefCount = ThunderGraft.m_lpDirectSound->AddRef();
		QDebugWriteEntry("PatchDirectSoundCreate : AddRef returned %u", nRefCount);
	}

	LeaveCriticalSection(&ThunderGraft.m_csThreadLock);

	return (bRetVal ? S_OK : E_UNEXPECTED);
}

HRESULT WINAPI CThunderGraftPlugin::PatchDirectSoundCreate8(OPTIONAL IN LPCGUID lpcGuid, OUT LPDIRECTSOUND8 *ppDS8, OPTIONAL IN LPUNKNOWN pUnkOuter)
{
	QDebugWriteEntry("PatchDirectSoundCreate8(0x%X, x, x)", lpcGuid);

	if (!ppDS8)
		return DSERR_INVALIDPARAM;
	if (pUnkOuter)
		return DSERR_NOAGGREGATION;

	EnterCriticalSection(&ThunderGraft.m_csThreadLock);

	BOOL bRetVal = ThunderGraft.DirectSoundCreateCommon(lpcGuid);
	QDebugWriteEntry("PatchDirectSoundCreate8 : DirectSoundCreateCommon returned %d", bRetVal);

	HRESULT result;
	if (bRetVal)
	{
		result = ThunderGraft.m_lpDirectSound->QueryInterface(IID_IDirectSound8, (void **)ppDS8);

		ThunderGraft.m_lpDirectSound->AddRef();
		DirectSoundReleasePtr DirectSoundRelease = (DirectSoundReleasePtr)ThunderGraft.GetPatchedFunction(ThunderGraft.m_lpDirectSound, IUNKNOWN_RELEASE_INDEX);
		ULONG nRefCount = DirectSoundRelease(ThunderGraft.m_lpDirectSound);
		QDebugWriteEntry("PatchDirectSoundCreate8 : QueryInterface returned 0x%X, reference count of %u", result, nRefCount);
	}

	LeaveCriticalSection(&ThunderGraft.m_csThreadLock);

	if (!bRetVal)
		return E_UNEXPECTED;
	else
		return result;
}

ULONG WINAPI CThunderGraftPlugin::PatchDirectSoundRelease(IN IDirectSound *lpDirectSound)
{
	assert(lpDirectSound);

	// There are a few cases we have to handle, here. If everything goes well, there should only be 2 references to an IDirectSound in the end: ours, and the one held by FMOD. At this point we need to destroy our sound system. This means the next call to Release will be from FMOD, and the last one should be our own. As we release our last reference with the original Release function (not this one), the only time we should ever end up with a count of 0 is when FMOD creates and frees IDirectSound interfaces internally during System::init (it happens).
	EnterCriticalSection(&ThunderGraft.m_csThreadLock);

	DirectSoundReleasePtr DirectSoundRelease = (DirectSoundReleasePtr)ThunderGraft.GetPatchedFunction(lpDirectSound, IUNKNOWN_RELEASE_INDEX);
	QDebugWriteEntry("PatchDirectSoundRelease : GetPatchedFunction returned 0x%X", DirectSoundRelease);

	assert(ThunderGraft.m_bInitialized);

	ULONG nRefCount;
	if (DirectSoundRelease)
		nRefCount = DirectSoundRelease(lpDirectSound);
	else
		// If it's NULL, then we're still being called inside FMOD's initialization code
		nRefCount = lpDirectSound->Release();

	QDebugWriteEntry("PatchDirectSoundRelease : IDirectSound::Release returned %u", nRefCount);

	if (nRefCount == 2)
	{
		assert(ThunderGraft.m_bSSCreated);
		assert(lpDirectSound == ThunderGraft.m_lpDirectSound);

		ThunderGraft.DestroySoundSystem();
		nRefCount = 0;	// Let the caller know the interface is freed
	}
	else if (nRefCount == 0)
		assert(!ThunderGraft.m_bSSCreated);

	LeaveCriticalSection(&ThunderGraft.m_csThreadLock);

	return nRefCount;
}

// You might wonder why we're redirecting both IDirectSound::CreateSoundBuffer and IDirectSound8::CreateSoundBuffer to the same function which takes an IDirectSound. The reason is that we're calling CreateSoundBuffer through the DirectSound interface we're passed. The call to CreateSoundBuffer will go to the right function because the v-table position of the two functions is one in the same, as IDirectSound8 is a child class of IDirectSound; the v-table itself that we get passed determines which version of the function we end up calling, and we don't have to worry about it at all.
HRESULT WINAPI CThunderGraftPlugin::PatchCreateSoundBuffer(IN IDirectSound *lpDirectSound, IN LPCDSBUFFERDESC lpBufferDesc, OUT LPDIRECTSOUNDBUFFER *lplpBuffer, OPTIONAL IN LPUNKNOWN lpUnkOuter)
{
	QDebugWriteEntry("PatchCreateSoundBuffer(x, 0x%X, x, 0x%X)", lpBufferDesc, lpUnkOuter);

	assert(lpDirectSound);

	HRESULT hResult = E_UNEXPECTED;
	EnterCriticalSection(&ThunderGraft.m_csThreadLock);

	assert(ThunderGraft.m_bInitialized);

	// All we need to do is simply create the buffer, patch the v-table, and return
	CreateSoundBufferPtr CreateSoundBuffer = (CreateSoundBufferPtr)ThunderGraft.GetPatchedFunction(lpDirectSound, IDIRECTSOUND_CREATESOUNDBUFFER_INDEX);
	QDebugWriteEntry("PatchCreateSoundBuffer : GetPatchedFunction returned 0x%X", CreateSoundBuffer);

	if (CreateSoundBuffer)
		hResult = CreateSoundBuffer(lpDirectSound, lpBufferDesc, lplpBuffer, lpUnkOuter);
	else
		hResult = lpDirectSound->CreateSoundBuffer(lpBufferDesc, lplpBuffer, lpUnkOuter);

	QDebugWriteEntry("PatchCreateSoundBuffer : CreateSoundBuffer returned 0x%X", hResult);
	if (SUCCEEDED(hResult) && (lpBufferDesc->dwFlags & DSBCAPS_PRIMARYBUFFER))
	{
		// Patch it up
		BOOL bRetVal = ThunderGraft.PatchIDirectSoundBuffer(*lplpBuffer);
		QDebugWriteEntry("PatchCreateSoundBuffer : PatchDirectSoundBuffer returned %d", bRetVal);
		if (!bRetVal)
			(*lplpBuffer)->Release();	// Failed to patch it. Clean up.
	}

	LeaveCriticalSection(&ThunderGraft.m_csThreadLock);

	return hResult;
}

HRESULT WINAPI CThunderGraftPlugin::PatchBufferSetFormat(IN IDirectSoundBuffer *lpBuffer, IN LPCWAVEFORMATEX lpFormat)
{
	QDebugWriteEntry("PatchBufferSetFormat(x, 0x%X)", lpFormat);

	assert(lpBuffer);

	if (!lpFormat)
		return DSERR_INVALIDPARAM;

	HRESULT hResult = E_UNEXPECTED;
	EnterCriticalSection(&ThunderGraft.m_csThreadLock);

	assert(ThunderGraft.m_bInitialized);

	// The format struct we're passed is constant; we need to modify it. This means we need to copy it then modify it.
	char buffer[512];	// Copy buffer
	WAVEFORMATEX *lpNewFormat = (WAVEFORMATEX *)buffer;
	memcpy(lpNewFormat, lpFormat, sizeof(WAVEFORMATEX) + lpFormat->cbSize);

	// If it's < 44 khz, set it to 44 khz
	if (lpFormat->wFormatTag == WAVE_FORMAT_PCM)
	{
		QDebugWriteEntry("PatchBufferSetFormat : Format: %u hz %u-bit %u channels", lpFormat->nSamplesPerSec, lpFormat->wBitsPerSample, lpFormat->nChannels);

		if (lpFormat->nSamplesPerSec < 44100)
		{
			lpNewFormat->nSamplesPerSec = 44100;
			lpNewFormat->nAvgBytesPerSec = lpNewFormat->nSamplesPerSec * lpNewFormat->wBitsPerSample / 8 * lpNewFormat->nChannels;
		}
	}

	BufferSetFormatPtr SetFormat = (BufferSetFormatPtr)ThunderGraft.GetPatchedFunction(lpBuffer, IDIRECTSOUNDBUFFER_SETFORMAT_INDEX);
	QDebugWriteEntry("PatchBufferSetFormat : GetPatchedFunction returned 0x%X", SetFormat);

	if (SetFormat)
		hResult = SetFormat(lpBuffer, lpNewFormat);
	else
		hResult = lpBuffer->SetFormat(lpNewFormat);

	QDebugWriteEntry("PatchBufferSetFormat : SetFormat returned 0x%X", hResult);

	LeaveCriticalSection(&ThunderGraft.m_csThreadLock);

	return hResult;
}