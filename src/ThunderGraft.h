/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2009 Justin Olbrantz. All Rights Reserved.
*/

/*
	ThunderGraft replaces the Storm DDA system (see DdaPlayer.h) with its own, which can play compressed formats. It hooks the DDA functions in the game's import table, and in GetProcAddress for games that look them up at runtime, and forwards them to a CDdaPlayer.

	The audio engine is started when the game initializes the DDA system, which it always does before using any other DDA function. Doing it there rather than in InitializePlugin keeps it out of the patcher's context, where waiting for threads might deadlock. If no audio engine can be started, the hooks forward to the original Storm functions instead, so the game behaves exactly as it would without ThunderGraft.
*/

#ifndef THUNDERGRAFT_H
#define THUNDERGRAFT_H

#include <windows.h>
#include <dsound.h>

#include <QHookAPI.h>
#include <MPQDraftPlugin.h>

#include "AudioEngine.h"
#include "DdaPlayer.h"
#include "Storm.h"

// Define the basic MPQDraft plugin constants, then import the plugin template
// The version comes from the build; see THUNDERGRAFT_VERSION in CMakeLists.txt
#ifdef THUNDERGRAFT_VERSION
#define BUILD_STRING "ThunderGraft v" THUNDERGRAFT_VERSION
#else
#define BUILD_STRING "ThunderGraft development build"
#endif
#ifdef _DEBUG
#define PLUGIN_ID 0x3c92af17
#define PLUGIN_NAME BUILD_STRING" Debug Build"
#else
#define PLUGIN_ID 0x3c92af16
#define PLUGIN_NAME BUILD_STRING
#endif

#include <MPQDPluginTemplate.h>

typedef FARPROC (WINAPI *GetProcAddressPtr)(IN HMODULE hModule, IN LPCSTR lpProcName);

// Creates the audio engine sounds are played with, or returns NULL if there is none. Defined by the audio backend that is linked in.
IAudioEngine *CreateAudioEngine();

class CThunderGraftPlugin : public CMPQDraftPluginTemplate
{
private:
	// Constant data

	// The various modules (usually DLLs) we need to recognize
	HMODULE m_hThunderGraft;
	HMODULE m_hHostModule;
	HMODULE m_hKernel32;
	HMODULE m_hStorm;

	// The various functions we'll patch/need internally. Some of these are set by GetFunctionPointers, some in PatchFunctionsInModule.
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

	// The audio engine, or NULL if there is none (yet) and the hooks forward to Storm. Deliberately never destroyed; see ~CDdaPlayer.
	IAudioEngine *volatile m_lpEngine;
	CDdaPlayer m_player;

	// Whether starting the audio engine has been attempted, and the lock that makes sure it only happens once
	BOOL m_bEngineStartAttempted;
	CRITICAL_SECTION m_csEngineStart;

	// Whether we have passed the destructor and hence should not do any unusual actions in the hook functions or other functions that might get called after DLL_PROCESS_DETACH (e.g. TerminatePlugin).
	volatile BOOL m_bShutDown;

	// The update thread and the event to kill it, if created
	HANDLE m_hUpdateThread;
	// Note that because of a race condition we can't actually close this handle; we can only leave it for Windows to clean up on shutdown.
	HANDLE m_hKillUpdateThread;

	// Patching functions
	// GetFunctionPointers and PatchFunctionsInModule aren't entirely distinct. GetFunctionPointers gets the addresses of the various modules and the function pointers that we don't patch, but getting the function pointers of functions we patch is deferred until PatchFunctionsInModule is called (as the same function performs both the patching and getting the function pointer for a function). This is pretty icky, but it seemed marginally better than traversing the import tables twice for each function we want to patch.
	BOOL GetFunctionPointers();
	BOOL PatchFunctionsInModule(IN HMODULE hModule);

	// The reason ppfnOldFunction is LPVOID is so that variable pointers can be implicitely cast to it. Note that this function serves a double purpose - if a pfnNewFunction is specified it patches the function, otherwise it simply gets the current address of that function.
	BOOL PatchImportEntry(IN HMODULE hHostProgram, IN HMODULE hExportModule, IN OPTIONAL LPCSTR lpszFunctionName, IN OPTIONAL DWORD nFunctionOrdinal, IN OPTIONAL LPVOID pfnNewFunction, OUT OPTIONAL LPVOID ppfnOldFunction);

	// Starts the audio engine, the player and the update thread, the first time it's called
	void StartAudioEngine();

	BOOL CreateUpdateThread();
	static unsigned __stdcall UpdateThread(IN void *lpThis);

	// Whether the hooks should use our player rather than forwarding to Storm
	BOOL UsingPlayer() const
	{ return m_lpEngine && !m_bShutDown; }

	// Function intercepts
	static FARPROC WINAPI PatchGetProcAddress(OPTIONAL IN HMODULE hModule, IN LPCSTR lpProcName);

	static BOOL WINAPI PatchCloseFile(IN HANDLE hFile);

	static BOOL WINAPI PatchDdaInitialize(IN IDirectSound *lpDirectSound);
	static BOOL WINAPI PatchDdaDestroy();

	static BOOL WINAPI PatchDdaBegin(IN HANDLE hFile, IN DWORD dwWAVBufferSize, IN DWORD dwFlags);
	static BOOL WINAPI PatchDdaBeginEx(IN HANDLE hFile, IN DWORD dwWAVBufferSize, IN DWORD dwFlags, OPTIONAL IN DWORD dwWAVReadPtr, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos, OPTIONAL IN IDirectSoundBuffer *lpSoundBuffer);
	static BOOL WINAPI PatchDdaEnd(HANDLE hFile);

	static BOOL WINAPI PatchDdaGetPos(IN HANDLE hFile, OPTIONAL OUT LPDWORD lpdwCurPos, OPTIONAL OUT LPDWORD lpdwLength);
	static BOOL WINAPI PatchDdaGetVolume(IN HANDLE hFile, OPTIONAL OUT long *lpnVolume, OPTIONAL OUT long* lpnPanPos);
	static BOOL WINAPI PatchDdaSetVolume(IN HANDLE hFile, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos);

public:
	CThunderGraftPlugin();
	~CThunderGraftPlugin();

	void SetThisModule(IN HMODULE hThunderGraft)
	{ m_hThunderGraft = hThunderGraft; }

	// IMPQDraftPlugin functions
	virtual BOOL WINAPI Configure(IN HWND hParentWnd);
	virtual BOOL WINAPI GetModules(OPTIONAL OUT MPQDRAFTPLUGINMODULE *lpPluginModules, OUT LPDWORD lpnNumModules);

	virtual BOOL WINAPI InitializePlugin(IN IMPQDraftServer *lpMPQDraftServer);
	virtual BOOL WINAPI TerminatePlugin();
};

extern CThunderGraftPlugin ThunderGraft;

#endif // #ifndef THUNDERGRAFT_H
