// Storm's SFile functions on real MPQs, through StormLib. Files can come from an archive or, as with Storm, from disk.

#ifndef MPQSTORM_H
#define MPQSTORM_H

#include <windows.h>
#include <string>
#include <vector>

#include "Storm.h"

namespace MpqStorm
{
	// A file to put in an archive, and how to store it: StormLib's MPQ_FILE_* flags, and MPQ_COMPRESSION_* for the first sector and for the rest
	struct ArchivedFile
	{
		std::string localPath;
		std::string archivedName;
		DWORD dwFlags;
		DWORD dwCompression;
		DWORD dwCompressionNext;
	};

	// Makes a new archive holding the files, replacing any file already at that path
	bool CreateArchive(const std::string &path, const std::vector<ArchivedFile> &files);

	// NULL on failure. Without an archive, files are looked for in the archives with the highest priority first, as Storm does.
	HANDLE OpenArchive(const std::string &path, DWORD dwPriority = 0);
	// Its files have to be closed first
	bool CloseArchive(HANDLE hMpq);

	// Opens a file in an archive or, without one, in every archive by priority (the most recently opened first among equals) and then on disk. NULL on failure.
	HANDLE OpenFile(HANDLE hMpq, const std::string &name);
	// The archive a file was opened from, or NULL for a file on disk
	bool GetFileArchive(HANDLE hFile, HANDLE *lphMpq);

	// Unlike StormLib's own, these fail on handles that weren't opened here instead of crashing, and can be used from several threads at once
	STORMFILEFUNCTIONS Functions();

	// A path for an archive in the temporary directory that no other test program uses
	std::string TempArchivePath(const std::string &name);
}

#endif
