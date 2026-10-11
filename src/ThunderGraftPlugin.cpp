/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2009 Justin Olbrantz. All Rights Reserved.
*/

#include "ThunderGraft.h"
#include "resource.h"

#include <QDebug.h>

static INT_PTR CALLBACK AboutDlgProc(
  HWND hwndDlg,  // handle to dialog box
  UINT uMsg,     // message
  WPARAM wParam, // first message parameter
  LPARAM lParam  // second message parameter
)
{
	(void)wParam;
	(void)lParam;

	if (uMsg == WM_COMMAND)
	{
		EndDialog(hwndDlg, 0);

		return TRUE;
	}

	return FALSE;	// default processing
}

BOOL WINAPI GetMPQDraftPlugin(OUT IMPQDraftPlugin **lppMPQDraftPlugin)
{
	QDebugWriteEntry("GetMPQDraftPlugin(%p)", lppMPQDraftPlugin);

	if (!lppMPQDraftPlugin)
		return FALSE;

	*lppMPQDraftPlugin = &ThunderGraft;

	return TRUE;
}

BOOL WINAPI CThunderGraftPlugin::Configure(IN HWND hParentWnd)
{
	QDebugWriteEntry("Configure(%p)", hParentWnd);

	// Nothing to configure, really. Just show an informational dialog box.
	DialogBox(m_hThunderGraft, MAKEINTRESOURCE(IDD_ABOUT), hParentWnd, AboutDlgProc);

	return TRUE;
}

BOOL WINAPI CThunderGraftPlugin::GetModules(OPTIONAL OUT MPQDRAFTPLUGINMODULE *lpPluginModules, OUT LPDWORD lpnNumModules)
{
	(void)lpPluginModules;

	if (!lpnNumModules)
		return FALSE;

	// Everything ThunderGraft needs is linked into the plugin itself
	*lpnNumModules = 0;

	return TRUE;
}

BOOL WINAPI CThunderGraftPlugin::InitializePlugin(IN IMPQDraftServer *lpMPQDraftServer)
{
	if (!lpMPQDraftServer)
		return FALSE;

	// One thing to note about this function is that only things that are considered possible to fail under normal circumstances get error messages. This is pretty much just to avoid having to write more of them.
	BOOL bLogOpened = QDebugOpenLogFile("ThunderGraft", NULL, 0);
	(void)bLogOpened;

	QDebugWriteEntry("InitializePlugin(%p)", lpMPQDraftServer);

	// Get any module handles and function pointers we're going to need in the game patching process
	if (!GetFunctionPointers())
	{
		MessageBox(NULL, "Fatal Error: This program does not appear to be a patchable game. ThunderGraft only supports Diablo, Diablo II, Starcraft, and Warcraft II BNE. The game will now close.", PLUGIN_NAME, MB_ICONSTOP | MB_OK);

		return FALSE;
	}

	// Patch any functions needed in the game patching process
	if (!PatchFunctionsInModule(m_hHostModule))
		return FALSE;

	// The audio engine is started later, when the game initializes the DDA system. Until then, the hooks forward everything to Storm.
	return TRUE;
}

BOOL WINAPI CThunderGraftPlugin::TerminatePlugin()
{
	// Some patchers (e.g. FireGraft) call TerminatePlugin on DLL_PROCESS_DETACH. Apart from the fact that the things we can do in DLL_PROCESS_DETACH are rather limited, because we can't control in what order DLL_PROCESS_DETACH notifications are dispatched it's possible that at this point our DLL_PROCESS_DETACH/~CThunderGraftPlugin has already been called. This means we can't do much here at all.

	return TRUE;
}
