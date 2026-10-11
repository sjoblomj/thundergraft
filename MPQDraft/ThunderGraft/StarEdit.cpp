/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2009 Justin Olbrantz. All Rights Reserved.
*/

/*
	There are at least three things we have to do to enable support for a variety of file types in StarEdit. First, we have to replace (or at least disable) the error checking code that verifies that files imported into maps are WAV files. Once the user selects a file to open, StarEdit opens the file and checks its format using the Windows MMIO functions (mmioOpen, etc.). If the file is found to not be a PCM WAV file, StarEdit shows an error message and fails; if it's an 8-bit file (the ADPCM compression function requires 16-bit PCM), StarEdit displays a warning and imports the file uncompressed.

	Second, we need to override the function that gets the length of sounds in the map; this is more than simply aesthetic - the length of the sound is used in triggers, so it needs to be accurate. StarEdit has a function that handles this. The function looks up the path of the sound through the map string tables, and loads the entire sound file into memory. It then uses the same MMIO API to compute the length of the file, before finally freeing the memory the sound was loaded into and returns the length in MS.

	Finally, we need to modify the file spec string passed to GetOpenFileName to select the file to import, to allow selection of non-WAV files.
*/

#include "stdafx.h"
#include "ThunderGraft.h"

// StarEdit functions we'll have to crack to make MP3 importing work
typedef LPCSTR (__fastcall *GetMapStringPtr)(DWORD iString);

typedef LPBYTE (__fastcall *LoadFileFromMapPtr)(LPCSTR lpFileName, LPDWORD lpFileSize);
typedef BOOL (__fastcall *UnloadFileFromMap)(LPBYTE lpFileData);

// Version numbers
#define STAREDIT_20060307_TIMESTAMP 0x3AD6A804 // Versions 1.08 to 1.16.1+ (built March 7, 2006)

using namespace FMOD;

BOOL CThunderGraftPlugin::GetStarEditVersion()
{
	QDebugWriteEntry("GetStarEditVersion()");

	assert(!m_bInitialized);
	assert(m_hStarEdit); 

	// Get the DOS header and verify it's a valid executable
	PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)m_hStarEdit;
	if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE)
		return FALSE;

	// Get the Windows header and verify it's a PE image
	LPDWORD lpdwSignature = LPDWORD((LPBYTE)m_hStarEdit + pDosHeader->e_lfanew);
	PIMAGE_FILE_HEADER pNTHeader = PIMAGE_FILE_HEADER((LPBYTE)m_hStarEdit + pDosHeader->e_lfanew + sizeof(DWORD));
	if (*lpdwSignature != IMAGE_NT_SIGNATURE)
		return FALSE;

	// It's a valid image. Get the linker date/time stamp.
	m_dwSETimeStamp = pNTHeader->TimeDateStamp;

	QDebugWriteEntry("GetStarEditVersion : Time stamp 0x%X", m_dwSETimeStamp);

	return TRUE;
}

BOOL CThunderGraftPlugin::PatchStarEditFunctions()
{
	QDebugWriteEntry("PatchStarEditFunctions()");

	assert(!m_bInitialized);
	assert(m_hStarEdit); 

	DWORD dwPrevProtect, dwBlahProtect;

	// The reason I chose to hardcode this is because the way things are executed may change from version to version, and a simple array of addresses might not be good enough.
	switch (m_dwSETimeStamp)
	{
		case STAREDIT_20060307_TIMESTAMP:
		{
			// For now we'll make a cheap patch - just remove import format checking entirely
			LPBYTE lpbyTarget = (LPBYTE)m_hStarEdit + 0x3CC83;
			DWORD dwLength = 251;

			VirtualProtect(lpbyTarget, dwLength, PAGE_EXECUTE_READWRITE, &dwPrevProtect);
			memset(lpbyTarget, 0x90, dwLength);	// NOPs
			VirtualProtect(lpbyTarget, dwLength, dwPrevProtect, &dwBlahProtect);

			// Intercept the GetOpenFileName call to import a sound file, so we can change the filter for the open dialog box. Perhaps in the future it would be better to just intercept calls to find the resource string. Meh.
			UINT_PTR *lpnGetOpenFileNameCall = (UINT_PTR *)((LPBYTE)m_hStarEdit + 0x3C6FB);

			VirtualProtect(lpnGetOpenFileNameCall, sizeof(UINT_PTR), PAGE_EXECUTE_READWRITE, &dwPrevProtect);
			*lpnGetOpenFileNameCall = (UINT_PTR)GetOpenSoundFileNameA - ((UINT_PTR)lpnGetOpenFileNameCall + sizeof(UINT_PTR));
			VirtualProtect(lpnGetOpenFileNameCall, sizeof(UINT_PTR), dwPrevProtect, &dwBlahProtect);

			// Intercept the GetSoundLengthFromIndex function
			LPBYTE lpbyGetSoundLenFunc = (LPBYTE)m_hStarEdit + 0x3CF60;
			UINT_PTR *lpnGetSoundJumpAddr = (UINT_PTR *)&lpbyGetSoundLenFunc[1];
			DWORD dwJumpSize = sizeof(UINT_PTR) + 1;

			VirtualProtect(lpbyGetSoundLenFunc, dwJumpSize, PAGE_EXECUTE_READWRITE, &dwPrevProtect);
			*lpbyGetSoundLenFunc = 0xE9;	// JMP 32-bit relative address
			*lpnGetSoundJumpAddr = (UINT_PTR)GetSoundLengthFromIndex - ((UINT_PTR)lpbyGetSoundLenFunc + dwJumpSize);
			VirtualProtect(lpbyGetSoundLenFunc, dwJumpSize, dwPrevProtect, &dwBlahProtect);

			return TRUE;
		}
	}

	return FALSE;
}
DWORD __fastcall CThunderGraftPlugin::GetSoundLengthFromIndex(DWORD iSound)
{
	QDebugWriteEntry("GetSoundLengthFromIndex(%d)", iSound);

	if (iSound == 0 || iSound > 512)	// Sound table is 512 entries, iSound 1-based
		return 0;

	DWORD dwSoundLength = 0;

	EnterCriticalSection(&ThunderGraft.m_csThreadLock);

	// If we don't have one already, create the sound system
	if (!ThunderGraft.m_bSSCreated)
	{
		BOOL bRetVal = ThunderGraft.InitializeSoundSystem(NULL);
		QDebugWriteEntry("GetSoundLengthFromIndex : InitializeSoundSystem returned %d", bRetVal);
	}

	if (ThunderGraft.m_bSSCreated)
	{
		// Get the length
		switch (ThunderGraft.m_dwSETimeStamp)
		{
			case STAREDIT_20060307_TIMESTAMP:
			{
				// Set up the function pointers we're going to need to do this
				GetMapStringPtr pfnGetMapString = (GetMapStringPtr)((LPBYTE)ThunderGraft.m_hStarEdit + 0x245E0);
				LoadFileFromMapPtr pfnLoadFileFromMap = (LoadFileFromMapPtr)((LPBYTE)ThunderGraft.m_hStarEdit + 0x159C0);
				UnloadFileFromMap pfnUnloadFileFromMap = (UnloadFileFromMap)((LPBYTE)ThunderGraft.m_hStarEdit + 0x15A90);

				// Get the string that contains the sound filename
				LPDWORD lpdwSoundTable = (LPDWORD)((LPBYTE)ThunderGraft.m_hStarEdit + 0x2071C0);
				DWORD iSoundString = lpdwSoundTable[iSound - 1];

				QDebugWriteEntry("GetSoundLengthFromIndex : Sound string index %d", iSoundString);

				if (iSoundString == 0 || iSoundString > 512)
					break;

				LPCSTR lpszSoundFileName = pfnGetMapString(iSoundString);
				QDebugWriteEntry("GetSoundLengthFromIndex : GetMapString returned %s", lpszSoundFileName ? lpszSoundFileName : "NULL");

				if (!lpszSoundFileName)
					break;

				// Load the sound into memory
				DWORD dwSoundSize;
				LPBYTE lpbySoundData = pfnLoadFileFromMap(lpszSoundFileName, &dwSoundSize);
				QDebugWriteEntry("GetSoundLengthFromIndex : LoadFileFromMap returned 0x%X, size %d", lpbySoundData, dwSoundSize);

				if (!lpbySoundData)
					break;

				// Create an FMOD stream on the memory location
				Sound *lpSound;
				DWORD dwMode = FMOD_OPENMEMORY | FMOD_OPENONLY | FMOD_IGNORETAGS | FMOD_MPEGSEARCH | FMOD_ACCURATETIME;
				FMOD_CREATESOUNDEXINFO soundParams;

				memset(&soundParams, 0, sizeof(soundParams));
				soundParams.cbsize = sizeof(soundParams);
				soundParams.length = dwSoundSize;

				FMOD_RESULT result = ThunderGraft.m_lpSoundSystem->createStream((LPCSTR)lpbySoundData, dwMode, &soundParams, &lpSound);
				QDebugWriteEntry("GetSoundLengthFromIndex : createSound returned %d", result);
				if (result == FMOD_OK)
				{
					// Finally, get the length of the sound is MS
					unsigned int nLengthMS;
					result = lpSound->getLength(&nLengthMS, FMOD_TIMEUNIT_MS);
					QDebugWriteEntry("GetSoundLengthFromIndex : createSound returned %d, length %u ms", result, nLengthMS);
					dwSoundLength = nLengthMS;

					// Done
					lpSound->release();
				}

				pfnUnloadFileFromMap(lpbySoundData);
			}
		}
	}

	LeaveCriticalSection(&ThunderGraft.m_csThreadLock);

	return dwSoundLength;
}

BOOL WINAPI CThunderGraftPlugin::GetOpenSoundFileNameA(LPOPENFILENAME lpofn)
{
	if (!lpofn)
		return FALSE;

	// Set the filter string
	lpofn->lpstrFilter = "Sound Files\0*.wav;*.mp3;*.ogg;*.aiff;*.flac;*.mp2;*.vag;*.wma\0";
	lpofn->lpstrCustomFilter = NULL;

	// Remove the option to set WAV compression level because we don't support it in this version
	lpofn->Flags &= ~OFN_ENABLETEMPLATE;

	return GetOpenFileNameA(lpofn);
}