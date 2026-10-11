/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2009 Justin Olbrantz. All Rights Reserved.
*/

#include "stdafx.h"
#include "ThunderGraft.h"
#include "resource.h"

INT_PTR CALLBACK AboutDlgProc(
  HWND hwndDlg,  // handle to dialog box
  UINT uMsg,     // message
  WPARAM wParam, // first message parameter
  LPARAM lParam  // second message parameter
)
{
	if (uMsg == WM_COMMAND)
	{
		EndDialog(hwndDlg, 0);

		return TRUE;
	}

	return FALSE;	// default processing
}

BOOL WINAPI GetMPQDraftPlugin(OUT IMPQDraftPlugin **lppMPQDraftPlugin)
{
	QDebugWriteEntry("GetMPQDraftPlugin(0x%X)", lppMPQDraftPlugin);

	if (!lppMPQDraftPlugin)
		return FALSE;

	*lppMPQDraftPlugin = &ThunderGraft;

	return TRUE;
}

BOOL WINAPI CThunderGraftPlugin::Configure(IN HWND hParentWnd)
{
	QDebugWriteEntry("Configure(0x%X)", hParentWnd);

	assert(hParentWnd);

	// Nothing to configure, really. Just show an informational dialog box.
	DialogBox(m_hThunderGraft, MAKEINTRESOURCE(IDD_ABOUT), hParentWnd, AboutDlgProc);
	
	return TRUE;
}

BOOL WINAPI CThunderGraftPlugin::GetModules(OPTIONAL OUT MPQDRAFTPLUGINMODULE *lpPluginModules, OUT LPDWORD lpnNumModules)
{
	if (!lpnNumModules)
		return FALSE;

	*lpnNumModules = 1;
	if (!lpPluginModules)
		return TRUE;

	// Look for the file in the search path first
	char szPathBuffer[MAX_PATH] = "";
	LPSTR lpszFilePart;
	DWORD dwRetVal = SearchPath(NULL, "fmodex.dll", NULL, sizeof(szPathBuffer), szPathBuffer, &lpszFilePart);
	QDebugWriteEntry("InitializePlugin : SearchPath returned %u, file name \"%s\"", dwRetVal, szPathBuffer);

	BOOL bRetVal = (dwRetVal && dwRetVal < sizeof(szPathBuffer));
	if (!bRetVal)
	{
		// Look for the file in the same directory as the plugin. In theory this should never fail. However, FireGraft loads libraries manually, without the support of Windows; this results in failure when we try to get the filename.
		bRetVal = GetModuleFileName(m_hThunderGraft, szPathBuffer, sizeof(szPathBuffer));
		QDebugWriteEntry("InitializePlugin : GetModuleFileName returned %d, file name \"%s\"", bRetVal, szPathBuffer);
		
		PathRemoveFileSpec(szPathBuffer);	// Return value does not indicate success
		bRetVal = (PathAppend(szPathBuffer, "fmodex.dll") &&
			PathFileExists(szPathBuffer));
	}
	if (!bRetVal)
	{
		MessageBox(NULL, "FMOD was not found. ThunderGraft requires the fmodex.dll file to be in the same directory as the ThunderGraft.qdp file or in the system search path.", "Unable to Patch", MB_ICONSTOP | MB_OK);
		return FALSE;
	}

	// Fill in the module fields
	DWORD dwBufferLen = sizeof(lpPluginModules->szModuleFileName);
	strncpy(lpPluginModules->szModuleFileName, szPathBuffer, dwBufferLen);
	lpPluginModules->szModuleFileName[dwBufferLen - 1] = '\0';

	lpPluginModules->dwComponentID = PLUGIN_ID;
	lpPluginModules->dwModuleID = FMOD_DLL_MODULE_ID;

	return TRUE;
}
BOOL WINAPI CThunderGraftPlugin::InitializePlugin(IN IMPQDraftServer *lpMPQDraftServer)
{
	assert(lpMPQDraftServer);

	// One thing to note about this function is that only things that are considered possible to fail under normal circumstances get error messages. This is pretty much just to avoid having to write more of them.
	assert(QDebugOpenLogFile("ThunderGraft", NULL, 0));

	QDebugWriteEntry("InitializePlugin(0x%X)", lpMPQDraftServer);

	//MessageBox(NULL, "Meow", "Moo", MB_OK);	// For debugging with FireGraft

	// Get the filename of the FMOD DLL
	char szFMODFileName[MAX_PATH] = "";
	BOOL bRetVal = lpMPQDraftServer->GetPluginModule(PLUGIN_ID, FMOD_DLL_MODULE_ID, szFMODFileName);
	QDebugWriteEntry("InitializePlugin : GetPluginModule returned %d, file name \"%s\"", bRetVal, szFMODFileName);
	if (!bRetVal)
		return FALSE;

	// Get any module handles and function pointers we're going to need in the game patching process
	if (!GetFunctionPointers())
	{
		MessageBox(NULL, "Fatal Error: This program does not appear to be a patchable game. ThunderGraft only supports Diablo, Diablo II, Starcraft, and Warcraft II BNE. The game will now close.", PLUGIN_NAME, MB_ICONSTOP | MB_OK);

		return FALSE;
	}

	// Load the FMOD DLL so we know it'll be there when we need it later
	// To allow for upgrading of FMOD after an SEMPQ has been created, first try to load FMOD through the search path
	m_hFMOD = LoadLibrary("fmodex.dll");
	QDebugWriteEntry("InitializePlugin : LoadLibrary returned 0x%X", m_hFMOD);
	if (!m_hFMOD)
	{
		// It's not in the search path. Use the one in the SEMPQ/MPQDraft directory.
		m_hFMOD = LoadLibrary(szFMODFileName);
		QDebugWriteEntry("InitializePlugin : LoadLibrary returned 0x%X", m_hFMOD);
	}
	if (!m_hFMOD)
		return FALSE;

	__pfnDliNotifyHook2 = DelayLoadHook;
	HRESULT result = __HrLoadAllImportsForDll2("fmodex.dll");
	QDebugWriteEntry("InitializePlugin : __HrLoadAllImportsForDll2 returned 0x%X", result);
	if (FAILED(result))
		return FALSE;

	// Patch any functions needed in the game patching process
	if (!PatchFunctionsInModule(m_hHostModule, "Storm.dll"))
		return FALSE;

	// Is this StarEdit?
	if (m_hStarEdit)
	{
		// See if it's a supported version; unlike game patching, StarEdit patching is version-specific.
		if (!GetStarEditVersion() ||
			!PatchStarEditFunctions())
		{
			MessageBox(NULL, "Fatal Error: Your version of StarEdit is not compatible with ThunderGraft. ThunderGraft supports at least StarCraft 1.08-1.16.1. StarEdit will now close.", PLUGIN_NAME, MB_ICONSTOP | MB_OK);
			return FALSE;
		}
	}

	EnterCriticalSection(&m_csThreadLock);

	// Create the update thread. Essentially the only reason this needs to be in the critical section is that this code causes a race condition reading/writing m_bInitialize> If the update thread got run before m_bInitialize is set, stuff could assert, and that would suck.
	bRetVal = CreateUpdateThread();
	if (bRetVal)
		m_bInitialized = TRUE;

	LeaveCriticalSection(&m_csThreadLock);

	return bRetVal;
}

BOOL WINAPI CThunderGraftPlugin::TerminatePlugin()
{
	// Some patchers (e.g. FireGraft) call TerminatePlugin on DLL_PROCESS_DETACH. Apart from the fact that the things we can do in DLL_PROCESS_DETACH are rather limited, because we can't control in what order DLL_PROCESS_DETACH notifications are dispatched it's possible that at this point our DLL_PROCESS_DETACH/~CThunderGraftPlugin has already been called. This means we can't do much here at all.

	return TRUE;
}