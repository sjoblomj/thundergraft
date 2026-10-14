#include "MpqStorm.h"

#include <StormLib.h>

#include <algorithm>

#include <map>
#include <set>
#include <vector>

namespace
{
	struct ArchiveEntry
	{
		HANDLE hMpq;
		DWORD dwPriority;
	};

	// StormLib isn't safe to use from several threads at once, and the plugin reads from its own threads while the game opens files. An SRW lock, as releasing it leaves the last error alone, which the functions report through.
	SRWLOCK g_lock = SRWLOCK_INIT;
	// In the order they were opened
	std::vector<ArchiveEntry> g_archives;
	// Each open file, and the archive it is from (NULL for files on disk)
	std::map<HANDLE, HANDLE> g_files;

	// Call with the lock held
	bool IsOpenArchive(HANDLE hMpq)
	{
		for (const ArchiveEntry &archive : g_archives)
		{
			if (archive.hMpq == hMpq)
				return true;
		}
		return false;
	}

	class CGuard
	{
	public:
		CGuard() { AcquireSRWLockExclusive(&g_lock); }
		~CGuard() { ReleaseSRWLockExclusive(&g_lock); }
	};

	// Call with the lock held. StormLib checks handles by reading what they point to.
	bool IsOpenFile(HANDLE hFile)
	{
		if (g_files.count(hFile))
			return true;

		SetLastError(ERROR_INVALID_HANDLE);
		return false;
	}

	BOOL WINAPI MpqCloseFile(HANDLE hFile)
	{
		CGuard guard;
		if (!IsOpenFile(hFile))
			return FALSE;

		g_files.erase(hFile);
		return SFileCloseFile(hFile);
	}

	BOOL WINAPI MpqReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nBytes, LPDWORD lpnRead, LPOVERLAPPED lpOverlapped)
	{
		CGuard guard;
		if (!IsOpenFile(hFile))
			return FALSE;

		return SFileReadFile(hFile, lpBuffer, nBytes, lpnRead, lpOverlapped);
	}

	DWORD WINAPI MpqGetFileSize(HANDLE hFile, LPDWORD lpnSizeHigh)
	{
		CGuard guard;
		if (!IsOpenFile(hFile))
			return INVALID_FILE_SIZE;

		return SFileGetFileSize(hFile, lpnSizeHigh);
	}

	DWORD WINAPI MpqSetFilePointer(HANDLE hFile, long nDistance, long *lpnDistanceHigh, DWORD dwMoveMethod)
	{
		CGuard guard;
		if (!IsOpenFile(hFile))
			return INVALID_SET_FILE_POINTER;

		return SFileSetFilePointer(hFile, nDistance, lpnDistanceHigh, dwMoveMethod);
	}
}

namespace
{
	BOOL WINAPI MpqOpenFileEx(HANDLE hMpq, LPCSTR lpszName, DWORD, HANDLE *lphFile)
	{
		*lphFile = MpqStorm::OpenFile(hMpq, lpszName);
		return *lphFile != NULL;
	}
}

namespace MpqStorm
{
	bool CreateArchive(const std::string &path, const std::vector<ArchivedFile> &files)
	{
		CGuard guard;
		DeleteFileA(path.c_str());

		// Room for the files and the (listfile)
		HANDLE hMpq = NULL;
		if (!SFileCreateArchive(path.c_str(), MPQ_CREATE_LISTFILE | MPQ_CREATE_ARCHIVE_V1, (DWORD)files.size() + 1, &hMpq))
			return false;

		bool bSuccess = true;
		for (const ArchivedFile &file : files)
		{
			if (!SFileAddFileEx(hMpq, file.localPath.c_str(), file.archivedName.c_str(), file.dwFlags, file.dwCompression, file.dwCompressionNext))
				bSuccess = false;
		}

		return SFileCloseArchive(hMpq) && bSuccess;
	}

	HANDLE OpenArchive(const std::string &path, DWORD dwPriority)
	{
		CGuard guard;
		HANDLE hMpq = NULL;
		if (!SFileOpenArchive(path.c_str(), 0, MPQ_OPEN_READ_ONLY, &hMpq))
			return NULL;

		g_archives.push_back(ArchiveEntry{hMpq, dwPriority});
		return hMpq;
	}

	bool CloseArchive(HANDLE hMpq)
	{
		CGuard guard;
		for (std::vector<ArchiveEntry>::iterator itArchive = g_archives.begin(); itArchive != g_archives.end(); itArchive++)
		{
			if (itArchive->hMpq == hMpq)
			{
				g_archives.erase(itArchive);
				return SFileCloseArchive(hMpq);
			}
		}

		SetLastError(ERROR_INVALID_HANDLE);
		return false;
	}

	HANDLE OpenFile(HANDLE hMpq, const std::string &name)
	{
		CGuard guard;
		if (hMpq && !IsOpenArchive(hMpq))
		{
			SetLastError(ERROR_INVALID_HANDLE);
			return NULL;
		}

		HANDLE hFile = NULL;
		if (hMpq)
		{
			if (!SFileOpenFileEx(hMpq, name.c_str(), SFILE_OPEN_FROM_MPQ, &hFile))
				return NULL;

			g_files[hFile] = hMpq;
			return hFile;
		}

		// Every archive, the highest priority first, and the most recently opened first among equals
		std::vector<ArchiveEntry> searched(g_archives.rbegin(), g_archives.rend());
		std::stable_sort(searched.begin(), searched.end(), [](const ArchiveEntry &a, const ArchiveEntry &b)
			{ return a.dwPriority > b.dwPriority; });
		for (const ArchiveEntry &archive : searched)
		{
			if (SFileOpenFileEx(archive.hMpq, name.c_str(), SFILE_OPEN_FROM_MPQ, &hFile))
			{
				g_files[hFile] = archive.hMpq;
				return hFile;
			}
		}

		if (!SFileOpenFileEx(NULL, name.c_str(), SFILE_OPEN_LOCAL_FILE, &hFile))
			return NULL;

		g_files[hFile] = NULL;
		return hFile;
	}

	bool GetFileArchive(HANDLE hFile, HANDLE *lphMpq)
	{
		CGuard guard;
		std::map<HANDLE, HANDLE>::iterator itFile = g_files.find(hFile);
		if (itFile == g_files.end())
		{
			SetLastError(ERROR_INVALID_HANDLE);
			return false;
		}

		*lphMpq = itFile->second;
		return true;
	}

	STORMFILEFUNCTIONS Functions()
	{
		STORMFILEFUNCTIONS storm;
		storm.SFileCloseFile = MpqCloseFile;
		storm.SFileReadFile = MpqReadFile;
		storm.SFileGetFileSize = MpqGetFileSize;
		storm.SFileSetFilePointer = MpqSetFilePointer;
		storm.SFileOpenFileEx = MpqOpenFileEx;
		return storm;
	}

	std::string TempArchivePath(const std::string &name)
	{
		char szTempPath[MAX_PATH] = "";
		GetTempPathA(sizeof(szTempPath), szTempPath);
		return std::string(szTempPath) + "ThunderGraft test " + name + " " + std::to_string(GetCurrentProcessId()) + ".mpq";
	}
}
