/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2009 Justin Olbrantz. All Rights Reserved.
*/

/*
	The Storm DDA system (SFileDda) is a small audio streaming system used for music and other sounds that are streamed (decoded on the fly). The system essentially consists of a streaming thread which asynchronously reads data from the files and copies it into the IDirectSoundBuffers. No decoding is necessary because the file is, after decompression by the MPQ decompression code, in PCM format, directly playable by the sound card once placed in the buffer. As such, the system is a fairly thin wrapper around DirectSound.
	
	The DDA system is initialized with an IDirectSound allocated and initialized by the host game. All DDA API functions refer to sounds by their SFile handle, which must be opened previously either from disk or from MPQ using the Storm SFile functions, and most flags and parameters (volume, panning, etc.) are specified in the native format of DirectSound. Reference-counting is used to ensure that SFileCloseFile does not close a file that is still playing (or, unrelated, still has outstanding asynchronous I/O requests) - the DDA system holds one reference on the file until the SFileDdaEnd function is called on it.
	
	IDirectSoundBuffers used to stream the sound are either created by SFileDdaBegin/Ex or passed as a parameter to SFileDdaBeginEx. If passed as a parameter, the game may update parameters of the stream such as volume directly through the IDirectSoundBuffer interface, rather than through the equivalent DDA functions such as SFileDdaSetVolume, which saves the overhead of locking the DDA mutex and looking up the IDirectSoundBuffer corresponding to the referenced SFile handle; Diablo II has also been observed to call GetStatus to determine if the sound is yet/still playing. To support this, we must emulate game-provided IDirectSoundBuffers by synchronizing the state between the buffer and the actual sound being played.

	We hook the DirectSoundCreate and DirectSoundCreate8 functions to allow us to initialize the FMOD sound system and get the IDirectSound interface that FMOD created; at the same time we hook the v-table functions for Release and CreateSoundBuffer. We hook Release to allow us to destroy the sound system when the game is done with it (as we can't really clean it up in TerminatePlugin or DLL_PROCESS_DETACH); we hook CreateSoundBuffer simply so that we can hook IDirectSoundBuffer::SetFormat, which we'll need to set the primary buffer to 44 khz.

	One other topic related to FMOD requires explanation. The fact that auxiliary files are packaged into SEMPQs and extracted with random file names means that having the linker link directly to the FMOD DLL was out of the question, and no static library of FMOD exists. Normally it's fine to just load such modules at runtime with LoadLibrary and GetProcAddress; but in this instance that's not really an option either, as FMOD exports non-abstract classes, with long mangled function names that are compiler-dependent. This left only one option: delay-loading the DLL; this allows us to let the linker do the work of binding functions, yet be able to load the DLL and import the functions after we figure out what it's been extracted as. To this end we use __HrLoadAllImportsForDll2 to perform the runtime linking, using the callback DelayLoadHook to handle the abnormal name.
*/

#ifndef THUNDERGRAFT_H
#define THUNDERGRAFT_H

#include "stdafx.h"

// Define the basic MPQDraft plugin constants, then import the plugin template
#define BUILD_STRING "ThunderGraft v2011.07.25.1"
#ifdef _DEBUG
#define PLUGIN_ID 0x3c92af17
#define PLUGIN_NAME BUILD_STRING" Debug Build"
#else
#define PLUGIN_ID 0x3c92af16
#define PLUGIN_NAME BUILD_STRING
#endif
#define FMOD_DLL_MODULE_ID 0xcd18af1f

#include <MPQDPluginTemplate.h>

// The helper that loads all functions for a delay-loaded DLL
HRESULT WINAPI __HrLoadAllImportsForDll2(IN LPCSTR szDll);

// The type of the thread start address for _beginthread(ex)
typedef unsigned (__stdcall *start_address)(IN void *);

typedef HMODULE (WINAPI *LoadLibraryAPtr)(IN LPCSTR lpFileName);
typedef FARPROC (WINAPI *GetProcAddressPtr)(IN HMODULE hModule, IN LPCSTR lpProcName);

// Storm functions we hook and/or use internally
typedef BOOL (WINAPI *SFileCloseFilePtr)(IN HANDLE hFile); // Ordinal 0xFD

typedef BOOL (WINAPI *SFileReadFilePtr)(IN HANDLE hFile, IN LPVOID lpBuffer, IN DWORD nNumberOfBytesToRead, OUT LPDWORD lpNumberOfBytesRead, OPTIONAL IN LPOVERLAPPED lpOverlapped); // Ordinal 0x10D
typedef DWORD (WINAPI *SFileGetFileSizePtr)(IN HANDLE hFile, OPTIONAL OUT LPDWORD lpFileSizeHigh); // Ordinal 0x109
typedef DWORD (WINAPI *SFileSetFilePointerPtr)(IN HANDLE hFile, IN long nDistanceToMove, OPTIONAL IN OUT long *lpDistanceToMoveHigh, IN DWORD dwMoveMethod); // Ordinal 0x10F

typedef BOOL (WINAPI *SFileDdaInitializePtr)(IN IDirectSound *lpDirectSound); // Ordinal 0x104
typedef BOOL (WINAPI *SFileDdaDestroyPtr)(); // Ordinal 0x100

typedef BOOL (WINAPI *SFileDdaBeginPtr)(IN HANDLE hFile, IN DWORD dwWAVBufferSize, IN DWORD dwFlags); // Ordinal 0xFE
typedef BOOL (WINAPI *SFileDdaBeginExPtr)(IN HANDLE hFile, IN DWORD dwWAVBufferSize, IN DWORD dwFlags, OPTIONAL IN DWORD dwWAVReadPtr, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos, OPTIONAL IN IDirectSoundBuffer *lpSoundBuffer); // Ordinal 0xFF
typedef BOOL (WINAPI *SFileDdaEndPtr)(IN HANDLE hFile); // Ordinal 0x101

typedef BOOL (WINAPI *SFileDdaGetPosPtr)(IN HANDLE hFile, OPTIONAL OUT LPDWORD lpdwCurPos, OPTIONAL OUT LPDWORD lpdwLength); // Ordinal 0x102
typedef BOOL (WINAPI *SFileDdaGetVolumePtr)(IN HANDLE hFile, OPTIONAL OUT long *lpnVolume, OPTIONAL OUT long *lpnPanPos); // Ordinal 0x103
typedef BOOL (WINAPI *SFileDdaSetVolumePtr)(IN HANDLE hFile, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos); // Ordinal 0x105

// DirectSound functions we hook
typedef HRESULT (WINAPI *DirectSoundCreatePtr)(OPTIONAL IN LPCGUID lpcGuid, OUT LPDIRECTSOUND *ppDS, OPTIONAL IN LPUNKNOWN pUnkOuter);
typedef HRESULT (WINAPI *DirectSoundCreate8Ptr)(OPTIONAL IN LPCGUID lpcGuid, OUT LPDIRECTSOUND8 *ppDS8, OPTIONAL IN LPUNKNOWN pUnkOuter);

typedef ULONG (WINAPI *DirectSoundReleasePtr)(IN IUnknown *lpDirectSound);
typedef HRESULT (WINAPI *CreateSoundBufferPtr)(IN IDirectSound *lpDirectSound, IN LPCDSBUFFERDESC lpBufferDesc, LPDIRECTSOUNDBUFFER *lpBuffer, LPUNKNOWN lpUnkOuter);
typedef HRESULT (WINAPI *BufferSetFormatPtr)(IN IDirectSoundBuffer *lpBuffer, LPCWAVEFORMATEX lpFormat);

// The maximum number of sounds that can be played at once through FMOD. Kind of arbitrary. Most of the time only 1 sound is playing at a time, though additional sounds may be played temporarily. As well, D2 fades music between areas, using multiple sounds. 6 should be plenty, but we can increase it if necessary
#define MAX_SOUND_CHANNELS 6

// Can be specified for initial volume and panning
#define SFILE_DDA_DEFAULT 0x7FFFFFFF

// The valid flags for SFileDdaBegin(Ex) are: SFILE_DDA_CTRLFREQUENCY, SFILE_DDA_CTRLPAN (volume control is always available), and SFILE_DDA_LOOPING
#define SFILE_DDA_CTRLPAN DSBCAPS_CTRLPAN
// Can specify it, but there's no way to set frequency, save for providing your own buffer. But in that case, these flags don't get used anyway...
#define SFILE_DDA_CTRLFREQUENCY DSBCAPS_CTRLFREQUENCY
#define SFILE_DDA_LOOPING 0x40000

// Various functions we need to intercept
#define IUNKNOWN_RELEASE_INDEX 2
#define IDIRECTSOUND_CREATESOUNDBUFFER_INDEX 3
#define IDIRECTSOUNDBUFFER_SETFORMAT_INDEX 14

struct PLAYINGSOUND
{
	FMOD::Sound *lpSound;
	FMOD::Channel *lpChannel;

	// Normal, non-global sounds automatically mute when the program playing them is minimized or loses focus. Global sounds continue playing even in these conditions.
	BOOL bGlobal;
	// The DDA system holds a reference to the currently playing file until SFileDdaEnd is called; this means that the file will not be freed when SFileCloseFile is called, if the DDA system still has a pointer to it. Since we can't access the reference count (that's a game-dependent internal field of the HSFILE), we have to emulate it by deferring the call to SFileCloseFile until we're done playing.
	BOOL bCloseOnStop;

	// Whether the sounds is still playing or has stopped. This is used to determine when a sound has just stopped.
	BOOL bIsPlaying;

	// The IDirectSoundBuffer we will get volume, pan, and frequency settings from, if it exists
	IDirectSoundBuffer *lpSoundBuffer;

	// The previous values. Used to determine whether to update the stream
	long nVolume;
	long nPanPos;
	long nFrequency;

	// The multiplier to apply to the IDirectSoundBuffer frequency. Diablo II seems to assume that all sounds play at 22050 hz, and uses that as a point of reference on its IDirectSoundBuffers it passes to us. We need to correct for that if the sound we're playing is at a different rate.
	float nFreqMult;

	PLAYINGSOUND(IN FMOD::Sound *lpSound, IN FMOD::Channel *lpChannel, IN BOOL bGlobal)
	{
		assert(lpSound);
		assert(lpChannel);

		this->lpSound = lpSound;
		this->lpChannel = lpChannel;

		this->bGlobal = bGlobal;
		this->bCloseOnStop = FALSE;
		this->bIsPlaying = TRUE;

		lpSoundBuffer = NULL;
	}
};

// Map to lookup playing sounds from their SFile handles
typedef stdext::hash_map<HANDLE, PLAYINGSOUND> PlayingSoundMap;

// Since we're having to make quite a few patchs to the v-tables (and to different functions that we redirect to our same patch functions), we need to keep track of all the originals. This maps pairs of pointers to the v-tables and the indices in those v-tables to the original pointer at that index. As v-tables should be constant data, we should only ever have to patch one copy of the v-table per interface we patch.
typedef pair<LPVOID *, DWORD> VTableEntry;
typedef stdext::hash_map<VTableEntry, LPVOID> VTablePatches;

template<> inline	size_t stdext::hash_value(IN const VTableEntry &key)
{ return hash_value((void *)key.first) + hash_value(key.second); }

// Just about everything is protected by m_csThreadLock
class CThunderGraftPlugin : public CMPQDraftPluginTemplate
{
private:
	// Constant data
	
	// The various modules (usually DLLs) we need to recognize
	HMODULE m_hThunderGraft;
	HMODULE m_hHostModule;
	HMODULE m_hKernel32;
	HMODULE m_hDirectSound;
	HMODULE m_hFMOD;
	HMODULE m_hStorm;

	// The various functions we'll patch/need internally. Some of these are set by GetFunctionPointers, some in PatchFunctions.
	LoadLibraryAPtr LoadLibraryA;
	GetProcAddressPtr GetProcAddress;

	SFileCloseFilePtr SFileCloseFile;

	SFileGetFileSizePtr SFileGetFileSize;
	SFileReadFilePtr SFileReadFile;
	SFileSetFilePointerPtr SFileSetFilePointer;
	
	SFileDdaBeginPtr SFileDdaBegin;
	SFileDdaBeginExPtr SFileDdaBeginEx;
	SFileDdaDestroyPtr SFileDdaDestroy;
	SFileDdaEndPtr SFileDdaEnd;
	SFileDdaGetPosPtr SFileDdaGetPos;
	SFileDdaGetVolumePtr SFileDdaGetVolume;
	SFileDdaInitializePtr SFileDdaInitialize;
	SFileDdaSetVolumePtr SFileDdaSetVolume;

	DirectSoundCreatePtr DirectSoundCreate;
	DirectSoundCreate8Ptr DirectSoundCreate8;

	// Used to protect nonconstant data
	CRITICAL_SECTION m_csThreadLock;

	// Non-constant data

	// The original addresses of the functions for any v-table patches we have to make
	VTablePatches m_vtablePatches;

	// Whether InitializePlugin has been called
	BOOL m_bInitialized;
	// Whether we have passed the destructor and hence should not do any unusual actions in the hook functions or other functions that might get called after DLL_PROCESS_DETACH (e.g. TerminatePlugin).
	BOOL m_bShutDown;

	// The sound system itself
	BOOL m_bSSCreated;
	FMOD::System *m_lpSoundSystem;
	IDirectSound *m_lpDirectSound;	// Counts as a reference

	// The update thread and the event to kill it, if created
	HANDLE m_hUpdateThread;
	// Note that because of a race condition we can't actually close this handle; we can only leave it for Windows to clean up on shutdown.
	HANDLE m_hKillUpdateThread;

	// The set of sounds currently playing
	PlayingSoundMap m_playingSoundMap;

	// Whether the target application was in the foreground in the last update. Used to determine whether to mute/unmute non-global sounds.
	BOOL m_bWasActivated;

	// Patching functions
	// GetFunctionPointers and PatchFunctions aren't entirely distinct. GetFunctionPointers gets the addresses of the various modules and the function pointers that we don't patch, but getting the function pointers of functions we patch is deferred until PatchFunctionsInModule is called (as the same function performs both the patching and getting the function pointer for a function). This is pretty icky, but it seemed marginally better than traversing the import tables twice for each function we want to patch.
	BOOL GetFunctionPointers();
	BOOL PatchFunctionsInModule(IN HMODULE hModule, IN LPCSTR lpszStormName);

	// The reason ppfnOldFunction is LPVOID is so that variable pointers can be implicitely cast to it. Note that this function serves a double purpose - if a pfnNewFunction is specified it patches the function, otherwise it simply gets the current address of that function.
	BOOL PatchImportEntry(IN HMODULE hHostProgram, IN HMODULE hExportModule, IN OPTIONAL LPCSTR lpszFunctionName, IN OPTIONAL DWORD nFunctionOrdinal, IN OPTIONAL LPVOID pfnNewFunction, OUT OPTIONAL LPVOID ppfnOldFunction);
	BOOL PatchObjectVtableEntry(IN LPVOID lpObject, IN DWORD iTableEntry, IN LPVOID lpfnNewFunction);
	FARPROC GetPatchedFunction(IN LPVOID lpObject, IN DWORD iTableEntry);
	BOOL PatchIDirectSound(IN IDirectSound *lpDirectSound);
	BOOL PatchIDirectSoundBuffer(IN IDirectSoundBuffer *lpBuffer);

	// Update list functions
	void UpdateSound(IN HANDLE hSFile, IN PLAYINGSOUND &playingSound, IN BOOL bActivated);

	BOOL CreateUpdateThread();
	static DWORD WINAPI UpdateThread(IN CThunderGraftPlugin *pThis);

	// Sound start/stop functions
	BOOL PlaySound(IN HANDLE hSFile, IN DWORD dwFlags, IN DWORD dwBufferBytes, OPTIONAL IN DWORD dwStartPos, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos, OPTIONAL IN IDirectSoundBuffer *lpSoundBuffer);
	BOOL StopSound(IN HANDLE hSFile);
	void StopAllSounds();

	// Sound system related functions
	BOOL InitializeSoundSystem(OPTIONAL IN LPCGUID lpGUID);
	// Configures the newly allocated m_lpSoundSystem after it's created by System_Create
	BOOL ConfigureSoundSystem(OPTIONAL IN LPCGUID lpGUID);
	// Chooses which driver to use based on the GUID (or default, if none). Returns -1 on failure.
	int SetDriverFromGUID(OPTIONAL IN LPCGUID lpGUID);
	// Common prologue to PatchDirectSoundCreate and PatchDirectSoundCreate8
	BOOL DirectSoundCreateCommon(OPTIONAL IN LPCGUID lpGUID);
	void DestroySoundSystem();

	void ShutDown();

	// Funky little stub needed to make delay-loading work when the DLL we're loading a DLL with a randomly-generated filename (think SEMPQs)
	static FARPROC WINAPI DelayLoadHook(IN unsigned dliNotify, IN PDelayLoadInfo pdli);

	// DirectSound takes exponential volume in 1/100 dB attenuation, FMOD uses linear from 0 to 1
	static float DirectSoundToFMODVolume(IN long nVolume)
	{
		if (nVolume >= DSBVOLUME_MIN)
			return pow(10.0f, nVolume / 2000.0f);
		else
			return 0;
	}

	// DirectSound takes exponential -abs(nPanPos) in 1/100 dB attenuation for the lesser side, FMOD uses linear from -1 to 1
	static float DirectSoundToFMODPan(IN long nPanPos)
	{
		long nExpAtten = -min(abs(nPanPos), -(DSBVOLUME_MIN));
		float nLinearAtten = pow(10.0f, nExpAtten / 2000.0f) - 1;

		return nLinearAtten * (nPanPos < 0 ? 1 : -1);
	}

	static float DirectSoundToFMODFrequency(IN DWORD nFrequency)
	{
		// Both specify frequency in hertz. Simply convert variable format.
		return (float)nFrequency;
	}

	static long FMODToDirectSoundVolume(IN float nVolume)
	{
		assert(nVolume >= 0 && nVolume <= 1);

		if (nVolume > 0)
			return min(max(long(log10(nVolume) * 2000.0f), DSBVOLUME_MIN), DSBVOLUME_MAX);
		else
			return DSBVOLUME_MIN;
	}

	static long FMODToDirectSoundPan(IN float nPanPos)
	{
		float nAbsPos = fabs(nPanPos);
		assert(nAbsPos <= 1);

		long nExpAtten;
		if (nAbsPos < 1)
			nExpAtten = long(log10(-nAbsPos + 1) * 2000.0f);
		else
			nExpAtten = DSBVOLUME_MIN;

		return nExpAtten * (nPanPos < 0 ? 1 : -1);
	}

	static long FMODToDirectSoundFrequency(IN float nFrequency)
	{
		return long(nFrequency + 0.5f);
	}

	// FMOD callback functions to read from MPQ files
	static FMOD_RESULT F_CALLBACK FMODFileOpenCallback(
		const char *  name, 
		int  unicode, 
		unsigned int *  filesize, 
		void **  handle, 
		void **  userdata
	);

	static FMOD_RESULT F_CALLBACK FMODFileCloseCallback(
		void *  handle, 
		void *  userdata
	);

	static FMOD_RESULT F_CALLBACK FMODFileReadCallback(
		void *  handle, 
		void *  buffer, 
		unsigned int  sizebytes, 
		unsigned int *  bytesread, 
		void *  userdata
	);

	static FMOD_RESULT F_CALLBACK FMODFileSeekCallback(
		void *  handle, 
		unsigned int  pos, 
		void *  userdata
	);

	// Function intercepts
	static HMODULE WINAPI PatchLoadLibraryA(IN LPCSTR lpFileName);
	static FARPROC WINAPI PatchGetProcAddress(OPTIONAL IN HMODULE hModule, IN LPCSTR lpProcName);

	static BOOL WINAPI PatchCloseFile(IN HANDLE hFile);

	static BOOL WINAPI PatchDdaInitialize(IN IDirectSound *lpDirectSound);
	static BOOL WINAPI PatchDdaDestroy();

	static BOOL WINAPI PatchDdaBegin(IN HANDLE hFile, IN DWORD dwWAVBufferSize, IN DWORD dwFlags); // Ordinal 0xFE
	static BOOL WINAPI PatchDdaBeginEx(IN HANDLE hFile, IN DWORD dwWAVBufferSize, IN DWORD dwFlags, OPTIONAL IN DWORD dwWAVReadPtr, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos, OPTIONAL IN IDirectSoundBuffer *lpSoundBuffer); // Ordinal 0xFF
	static BOOL WINAPI PatchDdaEnd(HANDLE hFile);

	static BOOL WINAPI PatchDdaGetPos(IN HANDLE hFile, OPTIONAL OUT LPDWORD lpdwCurPos, OPTIONAL OUT LPDWORD lpdwLength); // Ordinal 0x102
	static BOOL WINAPI PatchDdaGetVolume(IN HANDLE hFile, OPTIONAL OUT long *lpnVolume, OPTIONAL OUT long* lpnPanPos); // Ordinal 0x103
	static BOOL WINAPI PatchDdaSetVolume(IN HANDLE hFile, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos); // Ordinal 0x105

	static HRESULT WINAPI PatchDirectSoundCreate(OPTIONAL IN LPCGUID lpcGuid, OUT LPDIRECTSOUND *ppDS, OPTIONAL IN LPUNKNOWN pUnkOuter);
	static HRESULT WINAPI PatchDirectSoundCreate8(OPTIONAL IN LPCGUID lpcGuid, OUT LPDIRECTSOUND8 *ppDS, OPTIONAL IN LPUNKNOWN pUnkOuter);
	static ULONG WINAPI PatchDirectSoundRelease(IN IDirectSound *lpDirectSound);
	static HRESULT WINAPI PatchCreateSoundBuffer(IN IDirectSound *lpDirectSound, IN LPCDSBUFFERDESC lpBufferDesc, OUT LPDIRECTSOUNDBUFFER *lpBuffer, OPTIONAL IN LPUNKNOWN lpUnkOuter);
	static HRESULT WINAPI PatchBufferSetFormat(IN IDirectSoundBuffer *lpBuffer, IN LPCWAVEFORMATEX lpFormat);

public:
	CThunderGraftPlugin();
	~CThunderGraftPlugin();

	void SetThisModule(IN HMODULE hThunderGraft)
	{
		assert(hThunderGraft);

		m_hThunderGraft = hThunderGraft;
	}
	// IMPQDraftPlugin functions
	virtual BOOL WINAPI Configure(IN HWND hParentWnd);
	//virtual BOOL WINAPI ReadyForPatch();
	virtual BOOL WINAPI GetModules(OPTIONAL OUT MPQDRAFTPLUGINMODULE *lpPluginModules, OUT LPDWORD lpnNumModules);

	virtual BOOL WINAPI InitializePlugin(IN IMPQDraftServer *lpMPQDraftServer);
	virtual BOOL WINAPI TerminatePlugin();
};

extern CThunderGraftPlugin ThunderGraft;

#endif // #ifndef THUNDERGRAFT_H