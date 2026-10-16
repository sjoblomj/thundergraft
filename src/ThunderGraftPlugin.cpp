/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2009 Justin Olbrantz. All Rights Reserved.
*/

#include "ThunderGraft.h"
#include "resource.h"

#include <commdlg.h>
#include <string.h>

#include <QDebug.h>

namespace
{
	std::string Trim(const std::string &text)
	{
		size_t nStart = text.find_first_not_of(" \t");
		if (nStart == std::string::npos)
			return "";
		return text.substr(nStart, text.find_last_not_of(" \t") - nStart + 1);
	}

	std::string DebugLogFileText(HWND hwndDlg)
	{
		char szFile[MAX_PATH] = "";
		GetDlgItemTextA(hwndDlg, IDC_DEBUGLOGFILE, szFile, sizeof(szFile));
		return Trim(szFile);
	}

	// The file name and its Browse button are only for when debug logs are on
	void UpdateDebugLogControls(HWND hwndDlg)
	{
		BOOL bEnabled = IsDlgButtonChecked(hwndDlg, IDC_DEBUGLOG) == BST_CHECKED;
		EnableWindow(GetDlgItem(hwndDlg, IDC_DEBUGLOGFILE), bEnabled);
		EnableWindow(GetDlgItem(hwndDlg, IDC_BROWSE), bEnabled);
		EnableWindow(GetDlgItem(hwndDlg, IDC_DEBUGLOGHINT), bEnabled);
	}

	typedef BOOL (WINAPI *GetSaveFileNameAPtr)(LPOPENFILENAMEA lpofn);

	void BrowseForDebugLog(HWND hwndDlg)
	{
		// Loaded only here, so that the games don't load the common dialogs along with the plugin
		HMODULE hComDlg = LoadLibraryA("comdlg32.dll");
		GetSaveFileNameAPtr pfnGetSaveFileName = hComDlg ? (GetSaveFileNameAPtr)(void (*)(void))GetProcAddress(hComDlg, "GetSaveFileNameA") : NULL;
		if (!pfnGetSaveFileName)
		{
			if (hComDlg)
				FreeLibrary(hComDlg);
			return;
		}

		char szFile[MAX_PATH] = "";
		std::string current = DebugLogFileText(hwndDlg);
		if (current.size() < sizeof(szFile))
			strcpy(szFile, current.c_str());

		OPENFILENAMEA ofn;
		memset(&ofn, 0, sizeof(ofn));
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = hwndDlg;
		ofn.lpstrFilter = "Log files (*.log)\0*.log\0Text files (*.txt)\0*.txt\0All files\0*.*\0";
		ofn.lpstrFile = szFile;
		ofn.nMaxFile = sizeof(szFile);
		ofn.lpstrTitle = "Debug log";
		ofn.lpstrDefExt = "log";
		// No question about replacing an existing file: the log is started afresh each time the game starts anyway
		ofn.Flags = OFN_HIDEREADONLY | OFN_NOREADONLYRETURN | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

		if (pfnGetSaveFileName(&ofn))
			SetDlgItemTextA(hwndDlg, IDC_DEBUGLOGFILE, szFile);

		FreeLibrary(hComDlg);
	}

	// lParam of WM_INITDIALOG is the PluginSettings to show, which OK changes
	INT_PTR CALLBACK SettingsDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
		case WM_INITDIALOG:
		{
			PluginSettings *lpSettings = (PluginSettings *)lParam;
			SetWindowLongPtrA(hwndDlg, DWLP_USER, (LONG_PTR)lpSettings);

			SetWindowTextA(hwndDlg, PLUGIN_NAME);
			CheckDlgButton(hwndDlg, IDC_DEBUGLOG, lpSettings->bDebugLog ? BST_CHECKED : BST_UNCHECKED);
			SendDlgItemMessageA(hwndDlg, IDC_DEBUGLOGFILE, EM_LIMITTEXT, MAX_PATH - 1, 0);
			SetDlgItemTextA(hwndDlg, IDC_DEBUGLOGFILE, lpSettings->debugLogFile.c_str());
			UpdateDebugLogControls(hwndDlg);

			return TRUE;
		}

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
			case IDC_DEBUGLOG:
				UpdateDebugLogControls(hwndDlg);
				return TRUE;

			case IDC_BROWSE:
				BrowseForDebugLog(hwndDlg);
				return TRUE;

			case IDOK:
			{
				PluginSettings settings;
				settings.bDebugLog = IsDlgButtonChecked(hwndDlg, IDC_DEBUGLOG) == BST_CHECKED;
				std::string file = DebugLogFileText(hwndDlg);

				// The file name is kept while debug logs are off, but only if it can be used
				if (IsValidLogFileName(file))
					settings.debugLogFile = file;
				else if (settings.bDebugLog)
				{
					MessageBoxA(hwndDlg, file.empty() ? "Enter a file name for the debug log, or turn debug logs off." : "The debug log's file name can't contain any of the characters * ? \" < > |, or a colon other than after a drive letter.", PLUGIN_NAME, MB_ICONEXCLAMATION | MB_OK);
					SetFocus(GetDlgItem(hwndDlg, IDC_DEBUGLOGFILE));
					return TRUE;
				}

				*(PluginSettings *)GetWindowLongPtrA(hwndDlg, DWLP_USER) = settings;
				EndDialog(hwndDlg, IDOK);
				return TRUE;
			}

			case IDCANCEL:
				EndDialog(hwndDlg, IDCANCEL);
				return TRUE;
			}
			break;
		}

		return FALSE;	// default processing
	}
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

	PluginSettings settings = Settings();
	INT_PTR nResult = DialogBoxParamA(m_hThunderGraft, MAKEINTRESOURCEA(IDD_SETTINGS), hParentWnd, SettingsDlgProc, (LPARAM)&settings);
	if (nResult == -1)
		return FALSE;

	if (nResult == IDOK)
	{
		// Used for the patch even if they can't be remembered for next time
		Settings() = settings;
		RememberSettings(settings);
	}

	return TRUE;
}

BOOL WINAPI CThunderGraftPlugin::GetModules(OPTIONAL OUT MPQDRAFTPLUGINMODULE *lpPluginModules, OUT LPDWORD lpnNumModules)
{
	if (!lpnNumModules)
		return FALSE;

	// Everything else ThunderGraft needs is linked into the plugin itself
	const PluginSettings &settings = Settings();
	*lpnNumModules = settings.NeedsModule() ? 1 : 0;
	if (!lpPluginModules || !*lpnNumModules)
		return TRUE;

	// MPQDraft reads the file when it patches, or packs it into the SEMPQ, after which it is no longer needed. It is left behind, but is small and written over the next time.
	char szTempPath[MAX_PATH] = "";
	DWORD nLength = GetTempPathA(sizeof(szTempPath), szTempPath);
	if (!nLength || nLength >= sizeof(szTempPath))
		return FALSE;

	std::string path = std::string(szTempPath) + "ThunderGraft settings.ini";
	if (path.size() >= MPQDRAFT_MAX_PATH || !WriteSettingsFile(settings, path.c_str()))
		return FALSE;

	lpPluginModules[0].dwComponentID = PLUGIN_ID;
	lpPluginModules[0].dwModuleID = SETTINGS_MODULE_ID;
	lpPluginModules[0].bExecute = FALSE;
	strcpy(lpPluginModules[0].szModuleFileName, path.c_str());

	return TRUE;
}

BOOL WINAPI CThunderGraftPlugin::InitializePlugin(IN IMPQDraftServer *lpMPQDraftServer)
{
	if (!lpMPQDraftServer)
		return FALSE;

	// One thing to note about this function is that only things that are considered possible to fail under normal circumstances get error messages. This is pretty much just to avoid having to write more of them.
	OpenDebugLog(lpMPQDraftServer);

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
