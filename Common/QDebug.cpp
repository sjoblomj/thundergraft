/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2007 Justin Olbrantz. All Rights Reserved.
*/

#include <QDebug.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <algorithm>

// Global variables set up by QDebugOpenLogFileFn. The open and close functions aren't really thread safe, although QDebugWriteEntryFn is.
char g_szLogFileName[MAX_PATH + 1];

HANDLE g_hLogFile = INVALID_HANDLE_VALUE;

BOOL WINAPI QDebugOpenLogFileFn(LPCSTR lpszFileName)
{
	assert(lpszFileName);
	assert(g_hLogFile == INVALID_HANDLE_VALUE);

	if (!lpszFileName || g_hLogFile != INVALID_HANDLE_VALUE || strlen(lpszFileName) >= sizeof(g_szLogFileName))
		return FALSE;

	// Create the file, leaving it readable while it is being written
	HANDLE hLogFile = CreateFile(lpszFileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hLogFile == INVALID_HANDLE_VALUE)
		return FALSE;

	// Set up the global variables
	strcpy(g_szLogFileName, lpszFileName);

	g_hLogFile = hLogFile;

	return TRUE;
}

BOOL WINAPI QDebugCloseLogFileFn()
{
	// Fail quietly if a log file isn't open
	if (g_hLogFile == INVALID_HANDLE_VALUE)
		return FALSE;

	// Close the log file and clear the global variable
	CloseHandle(g_hLogFile);

	g_hLogFile = INVALID_HANDLE_VALUE;

	return TRUE;
}

BOOL WINAPI QDebugCloseDeleteLogFileFn()
{
	// Everything the same as with QDebugCloseLogFileFn, except...
	if (g_hLogFile == INVALID_HANDLE_VALUE)
		return FALSE;

	CloseHandle(g_hLogFile);

	g_hLogFile = INVALID_HANDLE_VALUE;

	// Delete the log file
	DeleteFile(g_szLogFileName);

	return TRUE;
}

BOOL __cdecl QDebugWriteEntryFn(LPCSTR lpszFormat, va_list params)
{
	assert(lpszFormat);

	// Don't rock the boat if opening the log file failed
	if (g_hLogFile == INVALID_HANDLE_VALUE)
		return FALSE;

	// We're going to create a log file entry of the form "[time] @[thread]: [user data]", followed by a line break
	// We're going to use something of a trick to make this operation thread-safe, where otherwise it wouldn't be (if you called WriteFile multiple times): we're going to form the entire log entry at once, and then write it with a single call to WriteFile. WriteFile is executed atomically, so we're guaranteed to not have conflicts with other threads. It's still possible, however, that the order the entries are written could shift a bit, if a thread gets stalled in between this function being called and WriteLine being called, but there's nothing we can do about that.
	DWORD dwStringOffset = 0;
	char szLogBuffer[4096];

	// Create the stamp
	SYSTEMTIME time;
	GetLocalTime(&time);

	// Leave room for the line break at the end. Entries too long for the buffer are truncated.
	const DWORD nTextSpace = sizeof(szLogBuffer) - 2;

	int nWritten = snprintf(szLogBuffer, nTextSpace, "%02u:%02u:%02u.%03u @%lu: ", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentThreadId());
	if (nWritten > 0)
		dwStringOffset = std::min((DWORD)nWritten, nTextSpace - 1);

	// Add on the log data
	nWritten = vsnprintf(&szLogBuffer[dwStringOffset], nTextSpace - dwStringOffset, lpszFormat, params);
	if (nWritten > 0)
		dwStringOffset = std::min(dwStringOffset + (DWORD)nWritten, nTextSpace - 1);

	memcpy(&szLogBuffer[dwStringOffset], "\r\n", 2);

	dwStringOffset += 2;

	// And write it
	DWORD dwBytesWritten;

	return WriteFile(g_hLogFile, szLogBuffer, dwStringOffset, &dwBytesWritten, NULL);
}
