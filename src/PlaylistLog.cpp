/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

#include "PlaylistLog.h"

#include <stdio.h>
#include <string.h>

CPlaylistLog::CPlaylistLog()
{
	InitializeCriticalSection(&m_csLock);
	m_hFile = INVALID_HANDLE_VALUE;
	m_bOpenFailed = false;
	m_bTrace = false;
}

void CPlaylistLog::Initialize(LPCSTR lpszTitle, LPCSTR lpszDirectory)
{
	EnterCriticalSection(&m_csLock);

	m_title = lpszTitle ? lpszTitle : "";
	m_directory = lpszDirectory ? lpszDirectory : "";

	char szSetting[64] = "";
	GetEnvironmentVariableA("THUNDERGRAFT_LOG", szSetting, sizeof(szSetting));
	m_bTrace = strstr(szSetting, "transitions") != NULL;

	LeaveCriticalSection(&m_csLock);
}

// Call with the lock held
bool CPlaylistLog::Open()
{
	if (m_hFile != INVALID_HANDLE_VALUE)
		return true;
	if (m_bOpenFailed)
		return false;

	std::string directories[2] = { m_directory, "" };
	char szTempPath[MAX_PATH] = "";
	if (GetTempPathA(sizeof(szTempPath), szTempPath))
		directories[1] = szTempPath;

	for (std::string &directory : directories)
	{
		if (directory.empty())
			continue;
		if (directory[directory.size() - 1] != '\\' && directory[directory.size() - 1] != '/')
			directory += '\\';

		// Started afresh each time the game starts
		m_hFile = CreateFileA((directory + PLAYLIST_LOG_NAME).c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (m_hFile != INVALID_HANDLE_VALUE)
		{
			m_directory = directory;
			break;
		}
	}

	if (m_hFile == INVALID_HANDLE_VALUE)
	{
		m_bOpenFailed = true;
		return false;
	}

	SYSTEMTIME now;
	GetLocalTime(&now);
	char szStarted[64];
	snprintf(szStarted, sizeof(szStarted), "%04u-%02u-%02u %02u:%02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
	std::string header = m_title + ", started " + szStarted + "\r\n";

	DWORD nWritten;
	WriteFile(m_hFile, header.data(), (DWORD)header.size(), &nWritten, NULL);

	return true;
}

void CPlaylistLog::WriteLine(const std::string &text)
{
	EnterCriticalSection(&m_csLock);

	try
	{
		if (Open())
		{
			std::string line = text + "\r\n";
			DWORD nWritten;
			WriteFile(m_hFile, line.data(), (DWORD)line.size(), &nWritten, NULL);
		}
	}
	catch (...)
	{
		// Out of memory; the line is lost
	}

	LeaveCriticalSection(&m_csLock);
}

std::string CPlaylistLog::Path()
{
	EnterCriticalSection(&m_csLock);
	std::string path = (m_hFile != INVALID_HANDLE_VALUE) ? m_directory + PLAYLIST_LOG_NAME : "";
	LeaveCriticalSection(&m_csLock);

	return path;
}

void CPlaylistLog::Write(const PlaylistDiagnostics &diagnostics)
{
	for (const PlaylistDiagnostic &diagnostic : diagnostics.All())
		Warning(diagnostic);
}

void CPlaylistLog::Write(const std::string &text)
{
	WriteLine(text);
}

void CPlaylistLog::Warning(const PlaylistDiagnostic &diagnostic)
{
	WriteLine(FormatPlaylistDiagnostic(diagnostic));
}

bool CPlaylistLog::TraceEnabled()
{
	return m_bTrace;
}

void CPlaylistLog::Trace(const std::string &text)
{
	SYSTEMTIME now;
	GetLocalTime(&now);
	char szTime[32];
	snprintf(szTime, sizeof(szTime), "%02u:%02u:%02u.%03u ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);

	WriteLine(szTime + text);
}
