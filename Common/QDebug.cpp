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
char g_szAppName[MAX_PATH + 1];
char g_szLogFileName[MAX_PATH + 1];

HANDLE g_hLogFile = INVALID_HANDLE_VALUE;

BOOL WINAPI QDebugOpenLogFileFn(LPCSTR lpszAppName, LPSTR lpszFileName, DWORD dwBufferLen)
{
	assert(lpszAppName);
	assert(g_hLogFile == INVALID_HANDLE_VALUE);

	// Get the temporary file path
	char szTempDirPath[MAX_PATH + 1];
	if (!GetTempPath(sizeof(szTempDirPath), szTempDirPath))
		return FALSE;

	// Build the filename to use for the log file from the app name, the date, and the time
	char szFileName[MAX_PATH + 1];
	SYSTEMTIME time;

	GetLocalTime(&time);

	snprintf(szFileName, sizeof(szFileName), "%s\\%s log - %.4d.%.2d.%.2d.%.2d.%.2d.%.2d.%.4d.txt", szTempDirPath, lpszAppName, time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);

	// Create the file
	HANDLE hLogFile = CreateFile(szFileName, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
	if (hLogFile == INVALID_HANDLE_VALUE)
		return FALSE;

	// Set up the global variables
	snprintf(g_szAppName, sizeof(g_szAppName), "%s", lpszAppName);
	strcpy(g_szLogFileName, szFileName);

	g_hLogFile = hLogFile;

	// If the caller wants it, copy the filename
	if (lpszFileName && dwBufferLen)
		snprintf(lpszFileName, dwBufferLen, "%s", szFileName);

	// It's possible that if two processes are started in sequence, they may generate the same filename and try to open that file for logging. We insert a short delay here to ensure that they will try to make different filenames.
	Sleep(50);

	return TRUE;
}

BOOL WINAPI QDebugCloseLogFileFn()
{
	// Fail quietly if a log file isn't open
	if (g_hLogFile == INVALID_HANDLE_VALUE)
		return FALSE;

	// Delay for the same reason as in QDebugOpenLogFileFn. Yes, I actually added these delays because of observed problems with this.
	Sleep(50);

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

	Sleep(50);

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

	int nWritten = snprintf(szLogBuffer, nTextSpace, "%d:%d:%d.%d @%lu: ", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentThreadId());
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
