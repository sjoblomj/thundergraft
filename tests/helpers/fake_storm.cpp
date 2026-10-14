/* A stand-in for Storm.dll that records calls, for testing the plugin's hooks without a game. The file functions are StormLib's, so tests can open real files from MPQs or from disk, and play them. */
#define BUILDING_FAKE_STORM
#include "fake_storm.h"

#include <string.h>
#include <string>
#include <vector>

#include <StormLib.h>

#include "fakes/MpqStorm.h"

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

/* The functions StormLib has too are called FakeSFile... here, and fake_storm.def exports them under Storm's names. */

extern "C" BOOL WINAPI FakeSFileCloseArchive(HANDLE hMpq)
{
	InterlockedIncrement(&g_callCounts[0xFC]);
	return MpqStorm::CloseArchive(hMpq);
}

/* Tests also close made-up handles, to see the call forwarded; those are only recorded */
extern "C" BOOL WINAPI FakeSFileCloseFile(HANDLE hFile)
{
	MpqStorm::Functions().SFileCloseFile(hFile);
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

/* The functions the game opens files with, and the plugin reads them with. They don't record their arguments, as the plugin calls them from its own threads. */
extern "C" DWORD WINAPI FakeSFileGetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh)
{
	InterlockedIncrement(&g_callCounts[0x109]);
	return MpqStorm::Functions().SFileGetFileSize(hFile, lpFileSizeHigh);
}

extern "C" BOOL WINAPI FakeSFileOpenArchive(LPCSTR lpszArchiveName, DWORD dwPriority, DWORD, HANDLE *phMpq)
{
	InterlockedIncrement(&g_callCounts[0x10A]);
	*phMpq = MpqStorm::OpenArchive(lpszArchiveName, dwPriority);
	return *phMpq != NULL;
}

BOOL WINAPI SFileOpenFile(LPCSTR lpszFileName, HANDLE *phFile)
{
	InterlockedIncrement(&g_callCounts[0x10B]);
	*phFile = MpqStorm::OpenFile(NULL, lpszFileName);
	return *phFile != NULL;
}

extern "C" BOOL WINAPI FakeSFileGetFileArchive(HANDLE hFile, HANDLE *phMpq)
{
	InterlockedIncrement(&g_callCounts[0x108]);
	return MpqStorm::GetFileArchive(hFile, phMpq);
}

extern "C" BOOL WINAPI FakeSFileOpenFileEx(HANDLE hMpq, LPCSTR lpszFileName, DWORD, HANDLE *phFile)
{
	InterlockedIncrement(&g_callCounts[0x10C]);
	*phFile = MpqStorm::OpenFile(hMpq, lpszFileName);
	return *phFile != NULL;
}

extern "C" BOOL WINAPI FakeSFileReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
	InterlockedIncrement(&g_callCounts[0x10D]);
	return MpqStorm::Functions().SFileReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}

extern "C" DWORD WINAPI FakeSFileSetFilePointer(HANDLE hFile, long nDistanceToMove, long *lpDistanceToMoveHigh, DWORD dwMoveMethod)
{
	InterlockedIncrement(&g_callCounts[0x10F]);
	return MpqStorm::Functions().SFileSetFilePointer(hFile, nDistanceToMove, lpDistanceToMoveHigh, dwMoveMethod);
}

BOOL WINAPI FakeStormCreateArchive(LPCSTR lpszArchiveName, const LPCSTR *lplpszFiles, DWORD nFiles)
{
	return FakeStormCreateArchiveEx(lpszArchiveName, lplpszFiles, NULL, nFiles);
}

BOOL WINAPI FakeStormCreateArchiveEx(LPCSTR lpszArchiveName, const LPCSTR *lplpszFiles, const LPCSTR *lplpszArchivedNames, DWORD nFiles)
{
	std::vector<MpqStorm::ArchivedFile> files;
	for (DWORD iFile = 0; iFile < nFiles; iFile++)
	{
		std::string path = lplpszFiles[iFile];
		MpqStorm::ArchivedFile file;
		file.localPath = path;
		file.archivedName = lplpszArchivedNames ? lplpszArchivedNames[iFile] : path.substr(path.find_last_of("\\/") + 1);
		file.dwFlags = MPQ_FILE_IMPLODE | MPQ_FILE_ENCRYPTED;
		file.dwCompression = 0;
		file.dwCompressionNext = 0;
		files.push_back(file);
	}

	return MpqStorm::CreateArchive(lpszArchiveName, files);
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
