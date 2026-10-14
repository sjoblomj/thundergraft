/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

#include "StormFileNames.h"

CStormFileNames::CStormFileNames()
{
	InitializeCriticalSection(&m_csLock);
}

void CStormFileNames::Set(HANDLE hFile, LPCSTR lpszName)
{
	if (!hFile || !lpszName)
		return;

	EnterCriticalSection(&m_csLock);
	try
	{ m_names[hFile] = lpszName; }
	catch (...)
	{
		// Out of memory. A stale name for a reused handle would be worse than none.
		m_names.erase(hFile);
	}
	LeaveCriticalSection(&m_csLock);
}

void CStormFileNames::Remove(HANDLE hFile)
{
	EnterCriticalSection(&m_csLock);
	m_names.erase(hFile);
	LeaveCriticalSection(&m_csLock);
}

bool CStormFileNames::Lookup(HANDLE hFile, std::string *lpName)
{
	bool bFound = false;

	EnterCriticalSection(&m_csLock);
	try
	{
		std::unordered_map<HANDLE, std::string>::iterator itName = m_names.find(hFile);
		if (itName != m_names.end())
		{
			*lpName = itName->second;
			bFound = true;
		}
	}
	catch (...)
	{ bFound = false; }
	LeaveCriticalSection(&m_csLock);

	return bFound;
}
