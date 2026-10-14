/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	The names the game opened its files with, by file handle. The DDA functions only get handles, but playlists go by file name, so ThunderGraft hooks SFileOpenFile(Ex) to remember each handle's name until SFileCloseFile.

	The open functions are called for almost every file the game opens, from several threads in Diablo II, so this has a lock of its own, held as briefly as possible, rather than sharing the DDA player's.
*/

#ifndef STORMFILENAMES_H
#define STORMFILENAMES_H

#include <windows.h>
#include <string>
#include <unordered_map>

class CStormFileNames
{
private:
	CRITICAL_SECTION m_csLock;
	std::unordered_map<HANDLE, std::string> m_names;

public:
	CStormFileNames();
	// Deliberately doesn't delete the lock, as the hooks may still be called while the process exits
	~CStormFileNames() {}

	// Never throws; a name that can't be stored is simply not known
	void Set(HANDLE hFile, LPCSTR lpszName);
	void Remove(HANDLE hFile);
	bool Lookup(HANDLE hFile, std::string *lpName);
};

#endif // #ifndef STORMFILENAMES_H
