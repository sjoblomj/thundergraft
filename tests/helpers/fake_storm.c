/* A stand-in for Storm.dll that records calls, for testing the plugin's hooks without a game. The file functions work on Win32 file handles, so tests can open real files with CreateFile and play them. */
#define BUILDING_FAKE_STORM
#include "fake_storm.h"

#include <string.h>

static volatile LONG g_callCounts[0x110];
static DWORD_PTR g_lastArgs[7];

static BOOL Record(DWORD nOrdinal, DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3, DWORD_PTR a4, DWORD_PTR a5, DWORD_PTR a6)
{
	InterlockedIncrement(&g_callCounts[nOrdinal]);
	g_lastArgs[0] = a0;
	g_lastArgs[1] = a1;
	g_lastArgs[2] = a2;
	g_lastArgs[3] = a3;
	g_lastArgs[4] = a4;
	g_lastArgs[5] = a5;
	g_lastArgs[6] = a6;
	return FAKE_STORM_RESULT(nOrdinal);
}

BOOL WINAPI SFileCloseFile(HANDLE hFile)
{
	CloseHandle(hFile);
	return Record(0xFD, (DWORD_PTR)hFile, 0, 0, 0, 0, 0, 0);
}

BOOL WINAPI SFileDdaBegin(HANDLE hFile, DWORD dwWAVBufferSize, DWORD dwFlags)
{ return Record(0xFE, (DWORD_PTR)hFile, dwWAVBufferSize, dwFlags, 0, 0, 0, 0); }

BOOL WINAPI SFileDdaBeginEx(HANDLE hFile, DWORD dwWAVBufferSize, DWORD dwFlags, DWORD dwWAVReadPtr, long nVolume, long nPanPos, IDirectSoundBuffer *lpSoundBuffer)
{ return Record(0xFF, (DWORD_PTR)hFile, dwWAVBufferSize, dwFlags, dwWAVReadPtr, (DWORD_PTR)nVolume, (DWORD_PTR)nPanPos, (DWORD_PTR)lpSoundBuffer); }

BOOL WINAPI SFileDdaDestroy(void)
{ return Record(0x100, 0, 0, 0, 0, 0, 0, 0); }

BOOL WINAPI SFileDdaEnd(HANDLE hFile)
{ return Record(0x101, (DWORD_PTR)hFile, 0, 0, 0, 0, 0, 0); }

BOOL WINAPI SFileDdaGetPos(HANDLE hFile, LPDWORD lpdwCurPos, LPDWORD lpdwLength)
{ return Record(0x102, (DWORD_PTR)hFile, (DWORD_PTR)lpdwCurPos, (DWORD_PTR)lpdwLength, 0, 0, 0, 0); }

BOOL WINAPI SFileDdaGetVolume(HANDLE hFile, long *lpnVolume, long *lpnPanPos)
{ return Record(0x103, (DWORD_PTR)hFile, (DWORD_PTR)lpnVolume, (DWORD_PTR)lpnPanPos, 0, 0, 0, 0); }

BOOL WINAPI SFileDdaInitialize(IDirectSound *lpDirectSound)
{ return Record(0x104, (DWORD_PTR)lpDirectSound, 0, 0, 0, 0, 0, 0); }

BOOL WINAPI SFileDdaSetVolume(HANDLE hFile, long nVolume, long nPanPos)
{ return Record(0x105, (DWORD_PTR)hFile, (DWORD_PTR)nVolume, (DWORD_PTR)nPanPos, 0, 0, 0, 0); }

/* The file functions the plugin reads sounds with. They don't record their arguments, as the plugin calls them from its own threads. */
DWORD WINAPI SFileGetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh)
{
	InterlockedIncrement(&g_callCounts[0x109]);
	return GetFileSize(hFile, lpFileSizeHigh);
}

BOOL WINAPI SFileReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
	DWORD nRead = 0;
	BOOL bSuccess;

	InterlockedIncrement(&g_callCounts[0x10D]);
	bSuccess = ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, &nRead, lpOverlapped);
	if (lpNumberOfBytesRead)
		*lpNumberOfBytesRead = nRead;

	/* Unlike ReadFile, Storm fails short reads with ERROR_HANDLE_EOF */
	if (bSuccess && nRead < nNumberOfBytesToRead)
	{
		SetLastError(ERROR_HANDLE_EOF);
		return FALSE;
	}

	return bSuccess;
}

DWORD WINAPI SFileSetFilePointer(HANDLE hFile, long nDistanceToMove, long *lpDistanceToMoveHigh, DWORD dwMoveMethod)
{
	InterlockedIncrement(&g_callCounts[0x10F]);
	return SetFilePointer(hFile, nDistanceToMove, lpDistanceToMoveHigh, dwMoveMethod);
}

LONG WINAPI FakeStormCallCount(DWORD nOrdinal)
{
	if (nOrdinal >= sizeof(g_callCounts) / sizeof(g_callCounts[0]))
		return -1;
	return g_callCounts[nOrdinal];
}

void WINAPI FakeStormLastArgs(DWORD_PTR args[7])
{
	memcpy(args, g_lastArgs, sizeof(g_lastArgs));
}
