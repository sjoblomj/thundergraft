/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2008 Justin Olbrantz. All Rights Reserved.
*/

#if !defined(MPQDRAFTPLUGINTEMPLATE_H)
#define MPQDRAFTPLUGINTEMPLATE_H

#include <windows.h>
#include <MPQDraftPlugin.h>

#if !defined(PLUGIN_ID)
#define PLUGIN_ID 0x12345678
#endif
#if !defined(PLUGIN_NAME)
#define PLUGIN_NAME "MPQDraft Plugin Template"
#endif

class CMPQDraftPluginTemplate : public IMPQDraftPlugin
{
public:
	virtual BOOL WINAPI Identify(LPDWORD lpdwPluginID)
	{
		if (!lpdwPluginID)
			return FALSE;

		*lpdwPluginID = PLUGIN_ID;

		return TRUE;
	}

	virtual BOOL WINAPI GetPluginName(LPSTR lpszPluginName, DWORD nNameBufferLength)
	{
		if (!nNameBufferLength)
			return FALSE;

		strncpy(lpszPluginName, PLUGIN_NAME, nNameBufferLength - 1);
		lpszPluginName[nNameBufferLength - 1] = '\0';

		return TRUE;
	}

	virtual BOOL WINAPI CanPatchExecutable(LPCSTR lpszEXEFileName)
	{
		if (!lpszEXEFileName)
			return FALSE;

		return TRUE;
	}

	virtual BOOL WINAPI Configure(HWND hParentWnd)
	{
		return TRUE;
	}

	virtual BOOL WINAPI ReadyForPatch()
	{
		return TRUE;
	}

	virtual BOOL WINAPI GetModules(MPQDRAFTPLUGINMODULE *lpPluginModules, LPDWORD lpnNumModules)
	{
		if (!lpnNumModules)
			return FALSE;

		*lpnNumModules = 0;

		return TRUE;
	}

	virtual BOOL WINAPI InitializePlugin(IMPQDraftServer *lpMPQDraftServer)
	{
		return TRUE;
	}

	virtual BOOL WINAPI TerminatePlugin()
	{
		return TRUE;
	}
};

BOOL WINAPI GetMPQDraftPlugin(IMPQDraftPlugin **lppMPQDraftPlugin);

#endif //#ifndef MPQDRAFTPLUGINTEMPLATE_H