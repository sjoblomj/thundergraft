/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	Creates ThunderGraft's audio engine with miniaudio.

	The THUNDERGRAFT_AUDIO environment variable can change this, mainly for testing:
	- "none": no engine; ThunderGraft forwards everything to Storm, as if it weren't there
	- "null": an engine whose output device plays nothing
*/

#include "ThunderGraft.h"
#include "MiniaudioEngine.h"

#include <process.h>
#include <string.h>

#include <QDebug.h>

namespace
{
	struct ENGINECREATION
	{
		MiniaudioOutput output;
		IAudioEngine *lpEngine;
	};

	unsigned __stdcall CreateEngineThread(void *lpParam)
	{
		ENGINECREATION *lpCreation = (ENGINECREATION *)lpParam;
		lpCreation->lpEngine = CreateMiniaudioEngine(lpCreation->output);

		return 0;
	}
}

IAudioEngine *CreateAudioEngine()
{
	char szSetting[16] = "";
	GetEnvironmentVariableA("THUNDERGRAFT_AUDIO", szSetting, sizeof(szSetting));
	QDebugWriteEntry("CreateAudioEngine : THUNDERGRAFT_AUDIO is \"%s\"", szSetting);

	if (_stricmp(szSetting, "none") == 0)
		return NULL;

	ENGINECREATION creation;
	creation.output = (_stricmp(szSetting, "null") == 0 ? MINIAUDIO_OUTPUT_NULL : MINIAUDIO_OUTPUT_DEFAULT);
	creation.lpEngine = NULL;

	// miniaudio initializes COM on the thread that creates the engine. That shouldn't be the game's thread, which may want COM set up differently, so it gets a thread of its own.
	HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, CreateEngineThread, &creation, 0, NULL);
	if (!hThread)
		return NULL;

	WaitForSingleObject(hThread, INFINITE);
	CloseHandle(hThread);

	QDebugWriteEntry("CreateAudioEngine : CreateMiniaudioEngine returned %p", creation.lpEngine);

	return creation.lpEngine;
}
