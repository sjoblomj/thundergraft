/* The functions exported by the fake Storm.dll. The SFile ones are exported by ordinal only, as in the real Storm. */
#ifndef FAKE_STORM_H
#define FAKE_STORM_H

#include <windows.h>
#include <dsound.h>

#ifdef BUILDING_FAKE_STORM
#define FAKE_STORM_API
#else
#define FAKE_STORM_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The file functions, which StormLib has too: the fake Storm itself doesn't see these declarations, as its own are StormLib's */
#ifndef BUILDING_FAKE_STORM
FAKE_STORM_API BOOL WINAPI SFileCloseArchive(HANDLE hMpq);
FAKE_STORM_API BOOL WINAPI SFileCloseFile(HANDLE hFile);
FAKE_STORM_API DWORD WINAPI SFileGetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);
FAKE_STORM_API BOOL WINAPI SFileOpenArchive(LPCSTR lpszArchiveName, DWORD dwPriority, DWORD dwFlags, HANDLE *phMpq);
/* Without an archive, opens a file on disk. The search scope is ignored. */
FAKE_STORM_API BOOL WINAPI SFileOpenFileEx(HANDLE hMpq, LPCSTR lpszFileName, DWORD dwSearchScope, HANDLE *phFile);
FAKE_STORM_API BOOL WINAPI SFileReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
FAKE_STORM_API DWORD WINAPI SFileSetFilePointer(HANDLE hFile, long nDistanceToMove, long *lpDistanceToMoveHigh, DWORD dwMoveMethod);
/* The archive a file was opened from; NULL for files on disk */
FAKE_STORM_API BOOL WINAPI SFileGetFileArchive(HANDLE hFile, HANDLE *phMpq);
#endif

/* Searches every archive, by priority, then the disk */
FAKE_STORM_API BOOL WINAPI SFileOpenFile(LPCSTR lpszFileName, HANDLE *phFile);

FAKE_STORM_API BOOL WINAPI SFileDdaBegin(HANDLE hFile, DWORD dwWAVBufferSize, DWORD dwFlags);
FAKE_STORM_API BOOL WINAPI SFileDdaBeginEx(HANDLE hFile, DWORD dwWAVBufferSize, DWORD dwFlags, DWORD dwWAVReadPtr, long nVolume, long nPanPos, IDirectSoundBuffer *lpSoundBuffer);
FAKE_STORM_API BOOL WINAPI SFileDdaDestroy(void);
FAKE_STORM_API BOOL WINAPI SFileDdaEnd(HANDLE hFile);
FAKE_STORM_API BOOL WINAPI SFileDdaGetPos(HANDLE hFile, LPDWORD lpdwCurPos, LPDWORD lpdwLength);
FAKE_STORM_API BOOL WINAPI SFileDdaGetVolume(HANDLE hFile, long *lpnVolume, long *lpnPanPos);
FAKE_STORM_API BOOL WINAPI SFileDdaInitialize(IDirectSound *lpDirectSound);
FAKE_STORM_API BOOL WINAPI SFileDdaSetVolume(HANDLE hFile, long nVolume, long nPanPos);

/* What the DDA functions and SFileCloseFile return: a value derived from the ordinal, so tests can tell the call really reached Storm */
#define FAKE_STORM_RESULT(ordinal) (0x5100 + (ordinal))

/* Test helpers, exported by name */
FAKE_STORM_API LONG WINAPI FakeStormCallCount(DWORD nOrdinal);
/* Makes an MPQ holding the files on disk, each imploded and encrypted, under its name without the directory */
FAKE_STORM_API BOOL WINAPI FakeStormCreateArchive(LPCSTR lpszArchiveName, const LPCSTR *lplpszFiles, DWORD nFiles);
/* The same, with the names to store them under, such as music\title.wav */
FAKE_STORM_API BOOL WINAPI FakeStormCreateArchiveEx(LPCSTR lpszArchiveName, const LPCSTR *lplpszFiles, const LPCSTR *lplpszArchivedNames, DWORD nFiles);
/* The arguments of the last call to any SFile function, as pointer-sized integers */
FAKE_STORM_API void WINAPI FakeStormLastArgs(DWORD_PTR args[7]);

#ifdef __cplusplus
}
#endif

#endif
