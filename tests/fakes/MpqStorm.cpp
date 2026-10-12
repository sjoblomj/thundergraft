#include "MpqStorm.h"

#include <StormLib.h>

#include <set>

namespace
{
	// StormLib isn't safe to use from several threads at once, and the plugin reads from its own threads while the game opens files. An SRW lock, as releasing it leaves the last error alone, which the functions report through.
	SRWLOCK g_lock = SRWLOCK_INIT;
	std::set<HANDLE> g_archives;
	std::set<HANDLE> g_files;

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

	HANDLE OpenArchive(const std::string &path)
	{
		CGuard guard;
		HANDLE hMpq = NULL;
		if (!SFileOpenArchive(path.c_str(), 0, MPQ_OPEN_READ_ONLY, &hMpq))
			return NULL;

		g_archives.insert(hMpq);
		return hMpq;
	}

	bool CloseArchive(HANDLE hMpq)
	{
		CGuard guard;
		if (!g_archives.erase(hMpq))
		{
			SetLastError(ERROR_INVALID_HANDLE);
			return false;
		}

		return SFileCloseArchive(hMpq);
	}

	HANDLE OpenFile(HANDLE hMpq, const std::string &name)
	{
		CGuard guard;
		if (hMpq && !g_archives.count(hMpq))
		{
			SetLastError(ERROR_INVALID_HANDLE);
			return NULL;
		}

		HANDLE hFile = NULL;
		if (!SFileOpenFileEx(hMpq, name.c_str(), hMpq ? SFILE_OPEN_FROM_MPQ : SFILE_OPEN_LOCAL_FILE, &hFile))
			return NULL;

		g_files.insert(hFile);
		return hFile;
	}

	STORMFILEFUNCTIONS Functions()
	{
		STORMFILEFUNCTIONS storm;
		storm.SFileCloseFile = MpqCloseFile;
		storm.SFileReadFile = MpqReadFile;
		storm.SFileGetFileSize = MpqGetFileSize;
		storm.SFileSetFilePointer = MpqSetFilePointer;
		return storm;
	}

	std::string TempArchivePath(const std::string &name)
	{
		char szTempPath[MAX_PATH] = "";
		GetTempPathA(sizeof(szTempPath), szTempPath);
		return std::string(szTempPath) + "ThunderGraft test " + name + " " + std::to_string(GetCurrentProcessId()) + ".mpq";
	}
}
