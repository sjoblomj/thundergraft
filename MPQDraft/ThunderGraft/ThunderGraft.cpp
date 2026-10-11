/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2009 Justin Olbrantz. All Rights Reserved.
*/

#include "ThunderGraft.h"

#include <assert.h>
#include <process.h>
#include <psapi.h>
#include <string.h>

#include <QDebug.h>

CThunderGraftPlugin ThunderGraft;

// Function pointers of different types have to go through FARPROC to be returned from GetProcAddress. Casting via void (*)(void) tells the compiler this is deliberate.
template <typename Function> static FARPROC ToFarProc(Function pfnFunction)
{
	return (FARPROC)(void (*)(void))pfnFunction;
}

// The only reason for this is that we need to save our module handle for later use
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	(void)lpReserved;

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
	m_hThunderGraft = NULL;
	m_hHostModule = NULL;
	m_hKernel32 = NULL;
	m_hStorm = NULL;

	GetProcAddress = NULL;

	SFileCloseFile = NULL;

	SFileGetFileSize = NULL;
	SFileReadFile = NULL;
	SFileSetFilePointer = NULL;

	SFileDdaBegin = NULL;
	SFileDdaBeginEx = NULL;
	SFileDdaDestroy = NULL;
	SFileDdaEnd = NULL;
	SFileDdaGetPos = NULL;
	SFileDdaGetVolume = NULL;
	SFileDdaInitialize = NULL;
	SFileDdaSetVolume = NULL;

	m_lpEngine = NULL;

	m_bShutDown = FALSE;

	m_hUpdateThread = NULL;
	m_hKillUpdateThread = NULL;
}

CThunderGraftPlugin::~CThunderGraftPlugin()
{
	QDebugWriteEntry("~CThunderGraftPlugin()");

	// This is a very minimalistic destructor by necessity. We're inside DllMain, which drastically restricts what we can do (e.g. trying to shut down the audio engine could deadlock waiting for its threads to exit). Furthermore, we still have hooks in various functions that we can't really remove, so we can't just shut down everything here. From here on the hooks forward everything to the original functions, and anything still alive is left to be cleaned up by Windows when the process terminates.
	m_bShutDown = TRUE;
}

BOOL CThunderGraftPlugin::CreateUpdateThread()
{
	QDebugWriteEntry("CreateUpdateThread()");

	assert(!m_hUpdateThread);
	assert(!m_hKillUpdateThread);

	m_hKillUpdateThread = CreateEvent(NULL, TRUE, FALSE, NULL);
	QDebugWriteEntry("CreateUpdateThread : CreateEvent returned %p", m_hKillUpdateThread);
	if (!m_hKillUpdateThread)
		return FALSE;

	unsigned int nThreadID;
	m_hUpdateThread = (HANDLE)_beginthreadex(NULL, 0, UpdateThread, this, 0, &nThreadID);
	QDebugWriteEntry("CreateUpdateThread : _beginthreadex returned %p", m_hUpdateThread);
	if (m_hUpdateThread)
		return TRUE;

	CloseHandle(m_hKillUpdateThread);
	m_hKillUpdateThread = NULL;

	return FALSE;
}

unsigned __stdcall CThunderGraftPlugin::UpdateThread(IN void *lpThis)
{
	CThunderGraftPlugin *pThis = (CThunderGraftPlugin *)lpThis;
	QDebugWriteEntry("UpdateThread(%p)", pThis);

	assert(pThis);

	BOOL bActivated = TRUE;
	do
	{
		// Determine whether we need to mute/unmute sounds. Polling like this may seem like a really crude method compared to something like window hooks to look for activate messages, but when you consider that doing so would intercept all messages, while this only runs once every so often, this way requires a lot less overhead.
		HWND hwndForeground = GetForegroundWindow();
		if (hwndForeground)
		{
			DWORD dwForeProcID;
			GetWindowThreadProcessId(hwndForeground, &dwForeProcID);
			bActivated = (dwForeProcID == GetCurrentProcessId());
		}

		pThis->m_player.Update(bActivated);
	} while (WaitForSingleObject(pThis->m_hKillUpdateThread, 30) == WAIT_TIMEOUT);

	QDebugWriteEntry("UpdateThread : Terminating update thread");

	return 0;
}

BOOL CThunderGraftPlugin::GetFunctionPointers()
{
	QDebugWriteEntry("GetFunctionPointers()");

	BOOL bRetVal;

	m_hHostModule = GetModuleHandle(NULL);
	assert(m_hHostModule);

	// Get the Kernel functions
	m_hKernel32 = GetModuleHandle("kernel32.dll");
	QDebugWriteEntry("GetFunctionPointers : Kernel32.dll found at %p", m_hKernel32);
	if (!m_hKernel32)
		return FALSE;

	// We have to get this here because we need to use it ourselves. As a result, we can't wait until PatchFunctionsInModule.
	bRetVal = PatchImportEntry(m_hHostModule, m_hKernel32, "GetProcAddress", 0, NULL, &GetProcAddress);
	QDebugWriteEntry("GetFunctionPointers : PatchImportEntry returned %d", bRetVal);

	// Get the Storm functions
	m_hStorm = GetModuleHandle("Storm.dll");
	QDebugWriteEntry("GetFunctionPointers : Storm.dll found at %p", m_hStorm);
	if (!m_hStorm)
		return FALSE;

	bRetVal = PatchImportEntry(m_hHostModule, m_hStorm, NULL, STORM_ORDINAL_SFILEGETFILESIZE, NULL, &SFileGetFileSize);
	QDebugWriteEntry("GetFunctionPointers : PatchImportEntry returned %d", bRetVal);
	bRetVal = PatchImportEntry(m_hHostModule, m_hStorm, NULL, STORM_ORDINAL_SFILEREADFILE, NULL, &SFileReadFile);
	QDebugWriteEntry("GetFunctionPointers : PatchImportEntry returned %d", bRetVal);
	bRetVal = PatchImportEntry(m_hHostModule, m_hStorm, NULL, STORM_ORDINAL_SFILESETFILEPOINTER, NULL, &SFileSetFilePointer);
	QDebugWriteEntry("GetFunctionPointers : PatchImportEntry returned %d", bRetVal);
	(void)bRetVal;

	// Make sure we have everything we need
	if (!GetProcAddress || !SFileGetFileSize || !SFileReadFile || !SFileSetFilePointer)
		return FALSE;

	return TRUE;
}

BOOL CThunderGraftPlugin::PatchFunctionsInModule(IN HMODULE hModule)
{
	QDebugWriteEntry("PatchFunctionsInModule(%p)", hModule);

	assert(hModule);
	assert(m_hKernel32);
	assert(m_hStorm);

	BOOL bRetVal;

	// The patching method used in ThunderGraft is both more robust and a lot trickier to use than the method employed in MPQDraft. Because other plugins may have already intercepted the functions we need to hook, we can't simply search the import tables for the precise function address - we have to look for the function name or ordinal. This causes a couple of problem. One, we have to make sure we don't intercept the same function twice; as we don't need to do otherwise, we trivially accomplish this by simply only patching once, patching only the modules imported by the game executable. Second, if other modules have already intercepted our functions, we need to call those - not the original, unpatched versions - from our own hook functions; this means we need to figure out which version of each function to use, which is handled by CThunderGraftPlugin::PatchImportEntry.

	// Patch the Kernel32 functions
	bRetVal = PatchImportEntry(hModule, m_hKernel32, "GetProcAddress", 0, (LPVOID)PatchGetProcAddress, NULL);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);

	// Patch the Storm functions
	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, STORM_ORDINAL_SFILEDDAINITIALIZE, (LPVOID)PatchDdaInitialize, &SFileDdaInitialize);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);
	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, STORM_ORDINAL_SFILEDDADESTROY, (LPVOID)PatchDdaDestroy, &SFileDdaDestroy);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);

	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, STORM_ORDINAL_SFILEDDABEGIN, (LPVOID)PatchDdaBegin, &SFileDdaBegin);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);
	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, STORM_ORDINAL_SFILEDDABEGINEX, (LPVOID)PatchDdaBeginEx, &SFileDdaBeginEx);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);
	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, STORM_ORDINAL_SFILEDDAEND, (LPVOID)PatchDdaEnd, &SFileDdaEnd);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);
	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, STORM_ORDINAL_SFILECLOSEFILE, (LPVOID)PatchCloseFile, &SFileCloseFile);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);

	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, STORM_ORDINAL_SFILEDDAGETPOS, (LPVOID)PatchDdaGetPos, &SFileDdaGetPos);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);
	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, STORM_ORDINAL_SFILEDDAGETVOLUME, (LPVOID)PatchDdaGetVolume, &SFileDdaGetVolume);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);
	bRetVal = PatchImportEntry(hModule, m_hStorm, NULL, STORM_ORDINAL_SFILEDDASETVOLUME, (LPVOID)PatchDdaSetVolume, &SFileDdaSetVolume);
	QDebugWriteEntry("PatchFunctionsInModule : PatchImportEntry returned %d", bRetVal);
	(void)bRetVal;

	// Make sure we have everything we need
	if (!SFileCloseFile ||
		!SFileDdaBegin || !SFileDdaBeginEx || !SFileDdaDestroy ||
		!SFileDdaEnd || !SFileDdaGetPos || !SFileDdaGetVolume ||
		!SFileDdaInitialize || !SFileDdaSetVolume)
		return FALSE;

	return TRUE;
}

BOOL CThunderGraftPlugin::PatchImportEntry(IN HMODULE hHostProgram, IN HMODULE hExportModule, IN OPTIONAL LPCSTR lpszFunctionName, IN OPTIONAL DWORD nFunctionOrdinal, IN OPTIONAL LPVOID pfnNewFunction, OUT OPTIONAL LPVOID ppfnOldFunction)
{
	QDebugWriteEntry("PatchImportEntry(%p, %p, %s, %lu, %p, %p)", hHostProgram, hExportModule, (lpszFunctionName ? lpszFunctionName : "NULL"), nFunctionOrdinal, pfnNewFunction, ppfnOldFunction);

	// This is essentially a wrapper around the function by the same name in QHookAPI.cpp. In addition to doing the patch and/or getting the list of function addresses, this one searches through the list of function addresses to find the one we should adopt as out "unpatched" version (the version of the function we call from without our patched functions.
	// The reason this is necessary is that either the patcher (e.g. FireGraft) or another plugin may have already patched the functions we want before we got called. If we overwrite the previous patches, we may break the patcher or other plugins; if we do a trivial function address search for the target function, we may break if another plugin has already altered the function address. Thus we get to do things the hard way.

	// Patch the module(s)
	ModuleFunctionMap moduleMap;
	DWORD nRetVal = ::PatchImportEntry(hHostProgram, hExportModule, lpszFunctionName, nFunctionOrdinal, (FARPROC)pfnNewFunction, &moduleMap, TRUE);
	QDebugWriteEntry("PatchImportEntry : ::PatchImportEntry returned %lu", nRetVal);
	if (nRetVal == (DWORD)-1)
		return FALSE;

	if (!ppfnOldFunction || *(FARPROC *)ppfnOldFunction)
		return TRUE;	// Don't care or we already have one

	// Determine which function we want to establish as the one we'll call from our hook function
	LPCSTR lpszFunction = (lpszFunctionName ? lpszFunctionName : (LPCSTR)(UINT_PTR)nFunctionOrdinal);
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

		QDebugWriteEntry("PatchImportEntry : GetModuleInformation returned %d, module located at 0x%lX-0x%lX", bRetVal, (unsigned long)nModuleBase, (unsigned long)nModuleEnd);
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

			QDebugWriteEntry("PatchImportEntry : Module %p has been patched to %p", itFunc->first, (void *)itFunc->second);

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

FARPROC WINAPI CThunderGraftPlugin::PatchGetProcAddress(OPTIONAL IN HMODULE hModule, IN LPCSTR lpProcName)
{
	// Do not execute any custom code if we're supposed to be shut down
	if (ThunderGraft.m_bShutDown)
		return ThunderGraft.GetProcAddress(hModule, lpProcName);

	// Need to override everything we patched the import tables of, here
	BOOL bNameIsOrd = (UINT_PTR)lpProcName < (64 << 10);	// Ordinals are 16-bits

	if (hModule == ThunderGraft.m_hStorm && bNameIsOrd)
	{
		switch ((UINT_PTR)lpProcName)
		{
			case STORM_ORDINAL_SFILECLOSEFILE:
				return ToFarProc(PatchCloseFile);
			case STORM_ORDINAL_SFILEDDABEGIN:
				return ToFarProc(PatchDdaBegin);
			case STORM_ORDINAL_SFILEDDABEGINEX:
				return ToFarProc(PatchDdaBeginEx);
			case STORM_ORDINAL_SFILEDDADESTROY:
				return ToFarProc(PatchDdaDestroy);
			case STORM_ORDINAL_SFILEDDAEND:
				return ToFarProc(PatchDdaEnd);
			case STORM_ORDINAL_SFILEDDAGETPOS:
				return ToFarProc(PatchDdaGetPos);
			case STORM_ORDINAL_SFILEDDAGETVOLUME:
				return ToFarProc(PatchDdaGetVolume);
			case STORM_ORDINAL_SFILEDDAINITIALIZE:
				return ToFarProc(PatchDdaInitialize);
			case STORM_ORDINAL_SFILEDDASETVOLUME:
				return ToFarProc(PatchDdaSetVolume);
		}
	}
	else if (hModule == ThunderGraft.m_hKernel32 && !bNameIsOrd)
	{
		if (strcmp(lpProcName, "GetProcAddress") == 0)
			return ToFarProc(PatchGetProcAddress);
	}

	return ThunderGraft.GetProcAddress(hModule, lpProcName);
}

// The DDA system is still initialized, even when we play the sounds, so that forwarding to Storm keeps working
BOOL WINAPI CThunderGraftPlugin::PatchDdaInitialize(IN IDirectSound *lpDirectSound)
{
	QDebugWriteEntry("PatchDdaInitialize(%p)", lpDirectSound);

	return ThunderGraft.SFileDdaInitialize(lpDirectSound);
}

BOOL WINAPI CThunderGraftPlugin::PatchDdaDestroy()
{
	QDebugWriteEntry("PatchDdaDestroy()");

	if (ThunderGraft.UsingPlayer())
		ThunderGraft.m_player.StopAll();

	return ThunderGraft.SFileDdaDestroy();
}

BOOL WINAPI CThunderGraftPlugin::PatchCloseFile(IN HANDLE hSFile)
{
	if (ThunderGraft.UsingPlayer())
		return ThunderGraft.m_player.CloseFile(hSFile);

	return ThunderGraft.SFileCloseFile(hSFile);
}

BOOL WINAPI CThunderGraftPlugin::PatchDdaBegin(IN HANDLE hSFile, IN DWORD dwWAVBufferSize, IN DWORD dwFlags)
{
	if (ThunderGraft.UsingPlayer())
		return ThunderGraft.m_player.Begin(hSFile, dwFlags, dwWAVBufferSize, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, NULL);

	return ThunderGraft.SFileDdaBegin(hSFile, dwWAVBufferSize, dwFlags);
}

// dwWAVBufferSize and dwWAVReadPtr are in bytes
BOOL WINAPI CThunderGraftPlugin::PatchDdaBeginEx(IN HANDLE hSFile, IN DWORD dwWAVBufferSize, IN DWORD dwFlags, OPTIONAL IN DWORD dwWAVReadPtr, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos, OPTIONAL IN IDirectSoundBuffer *lpSoundBuffer)
{
	if (ThunderGraft.UsingPlayer())
		return ThunderGraft.m_player.Begin(hSFile, dwFlags, dwWAVBufferSize, dwWAVReadPtr, nVolume, nPanPos, lpSoundBuffer);

	return ThunderGraft.SFileDdaBeginEx(hSFile, dwWAVBufferSize, dwFlags, dwWAVReadPtr, nVolume, nPanPos, lpSoundBuffer);
}

BOOL WINAPI CThunderGraftPlugin::PatchDdaEnd(IN HANDLE hSFile)
{
	if (ThunderGraft.UsingPlayer())
		return ThunderGraft.m_player.End(hSFile);

	return ThunderGraft.SFileDdaEnd(hSFile);
}

BOOL WINAPI CThunderGraftPlugin::PatchDdaGetPos(IN HANDLE hSFile, OPTIONAL OUT LPDWORD lpdwCurPos, OPTIONAL OUT LPDWORD lpdwLength)
{
	if (ThunderGraft.UsingPlayer())
		return ThunderGraft.m_player.GetPos(hSFile, lpdwCurPos, lpdwLength);

	return ThunderGraft.SFileDdaGetPos(hSFile, lpdwCurPos, lpdwLength);
}

BOOL WINAPI CThunderGraftPlugin::PatchDdaGetVolume(IN HANDLE hSFile, OPTIONAL OUT long *lpnVolume, OPTIONAL OUT long *lpnPanPos)
{
	if (ThunderGraft.UsingPlayer())
		return ThunderGraft.m_player.GetVolume(hSFile, lpnVolume, lpnPanPos);

	return ThunderGraft.SFileDdaGetVolume(hSFile, lpnVolume, lpnPanPos);
}

BOOL WINAPI CThunderGraftPlugin::PatchDdaSetVolume(IN HANDLE hSFile, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos)
{
	if (ThunderGraft.UsingPlayer())
		return ThunderGraft.m_player.SetVolume(hSFile, nVolume, nPanPos);

	return ThunderGraft.SFileDdaSetVolume(hSFile, nVolume, nPanPos);
}
