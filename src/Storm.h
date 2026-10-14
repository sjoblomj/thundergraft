/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2009 Justin Olbrantz. All Rights Reserved.
*/

/*
	Declarations for the parts of Storm.dll that ThunderGraft hooks or uses. Storm exports these functions by ordinal only.
*/

#ifndef STORM_H
#define STORM_H

#include <windows.h>
#include <dsound.h>

// Ordinals of the Storm functions we hook and/or use internally
#define STORM_ORDINAL_SFILECLOSEARCHIVE 0xFC
#define STORM_ORDINAL_SFILECLOSEFILE 0xFD
#define STORM_ORDINAL_SFILEDDABEGIN 0xFE
#define STORM_ORDINAL_SFILEDDABEGINEX 0xFF
#define STORM_ORDINAL_SFILEDDADESTROY 0x100
#define STORM_ORDINAL_SFILEDDAEND 0x101
#define STORM_ORDINAL_SFILEDDAGETPOS 0x102
#define STORM_ORDINAL_SFILEDDAGETVOLUME 0x103
#define STORM_ORDINAL_SFILEDDAINITIALIZE 0x104
#define STORM_ORDINAL_SFILEDDASETVOLUME 0x105
#define STORM_ORDINAL_SFILEGETFILEARCHIVE 0x108
#define STORM_ORDINAL_SFILEGETFILESIZE 0x109
#define STORM_ORDINAL_SFILEOPENARCHIVE 0x10A
#define STORM_ORDINAL_SFILEOPENFILE 0x10B
#define STORM_ORDINAL_SFILEOPENFILEEX 0x10C
#define STORM_ORDINAL_SFILEREADFILE 0x10D
#define STORM_ORDINAL_SFILESETFILEPOINTER 0x10F

typedef BOOL (WINAPI *SFileCloseFilePtr)(IN HANDLE hFile);

typedef BOOL (WINAPI *SFileOpenArchivePtr)(IN LPCSTR lpszArchiveName, IN DWORD dwPriority, IN DWORD dwFlags, OUT HANDLE *lphMpq);
typedef BOOL (WINAPI *SFileCloseArchivePtr)(IN HANDLE hMpq);
typedef BOOL (WINAPI *SFileOpenFilePtr)(IN LPCSTR lpszFileName, OUT HANDLE *lphFile);
// Without an archive, searches every archive by priority, then the disk. With one, only that archive is meant to be searched, but MPQDraft's redirection makes even those calls search every archive.
typedef BOOL (WINAPI *SFileOpenFileExPtr)(OPTIONAL IN HANDLE hMpq, IN LPCSTR lpszFileName, IN DWORD dwSearchScope, OUT HANDLE *lphFile);
// The archive a file was opened from. Fails, or gives NULL, for files opened from disk.
typedef BOOL (WINAPI *SFileGetFileArchivePtr)(IN HANDLE hFile, OUT HANDLE *lphMpq);

typedef BOOL (WINAPI *SFileReadFilePtr)(IN HANDLE hFile, IN LPVOID lpBuffer, IN DWORD nNumberOfBytesToRead, OUT LPDWORD lpNumberOfBytesRead, OPTIONAL IN LPOVERLAPPED lpOverlapped);
typedef DWORD (WINAPI *SFileGetFileSizePtr)(IN HANDLE hFile, OPTIONAL OUT LPDWORD lpFileSizeHigh);
typedef DWORD (WINAPI *SFileSetFilePointerPtr)(IN HANDLE hFile, IN long nDistanceToMove, OPTIONAL IN OUT long *lpDistanceToMoveHigh, IN DWORD dwMoveMethod);

typedef BOOL (WINAPI *SFileDdaInitializePtr)(IN IDirectSound *lpDirectSound);
typedef BOOL (WINAPI *SFileDdaDestroyPtr)();

typedef BOOL (WINAPI *SFileDdaBeginPtr)(IN HANDLE hFile, IN DWORD dwWAVBufferSize, IN DWORD dwFlags);
typedef BOOL (WINAPI *SFileDdaBeginExPtr)(IN HANDLE hFile, IN DWORD dwWAVBufferSize, IN DWORD dwFlags, OPTIONAL IN DWORD dwWAVReadPtr, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos, OPTIONAL IN IDirectSoundBuffer *lpSoundBuffer);
typedef BOOL (WINAPI *SFileDdaEndPtr)(IN HANDLE hFile);

typedef BOOL (WINAPI *SFileDdaGetPosPtr)(IN HANDLE hFile, OPTIONAL OUT LPDWORD lpdwCurPos, OPTIONAL OUT LPDWORD lpdwLength);
typedef BOOL (WINAPI *SFileDdaGetVolumePtr)(IN HANDLE hFile, OPTIONAL OUT long *lpnVolume, OPTIONAL OUT long *lpnPanPos);
typedef BOOL (WINAPI *SFileDdaSetVolumePtr)(IN HANDLE hFile, OPTIONAL IN long nVolume, OPTIONAL IN long nPanPos);

// Can be specified for initial volume and panning
#define SFILE_DDA_DEFAULT 0x7FFFFFFF

// The valid flags for SFileDdaBegin(Ex) are: SFILE_DDA_CTRLFREQUENCY, SFILE_DDA_CTRLPAN (volume control is always available), and SFILE_DDA_LOOPING
#define SFILE_DDA_CTRLPAN DSBCAPS_CTRLPAN
// Can specify it, but there's no way to set frequency, save for providing your own buffer. But in that case, these flags don't get used anyway...
#define SFILE_DDA_CTRLFREQUENCY DSBCAPS_CTRLFREQUENCY
#define SFILE_DDA_LOOPING 0x40000

// The Storm file functions the DDA emulation needs to read and close files, and to open the tracks of playlists
struct STORMFILEFUNCTIONS
{
	SFileCloseFilePtr SFileCloseFile;
	SFileReadFilePtr SFileReadFile;
	SFileGetFileSizePtr SFileGetFileSize;
	SFileSetFilePointerPtr SFileSetFilePointer;
	// May be NULL, in which case no playlist track can be opened
	SFileOpenFileExPtr SFileOpenFileEx;
};

#endif // #ifndef STORM_H
