#include "FakeStorm.h"

#include <map>
#include <mutex>

namespace
{
	struct FakeFile
	{
		std::string contents;
		size_t nPosition = 0;
		int nCloseCount = 0;
	};

	std::mutex g_lock;
	std::map<HANDLE, FakeFile> g_files;
	UINT_PTR g_nNextHandle = 0x1000;

	FakeFile *Find(HANDLE hFile)
	{
		std::map<HANDLE, FakeFile>::iterator itFile = g_files.find(hFile);
		return itFile != g_files.end() ? &itFile->second : nullptr;
	}

	BOOL WINAPI FakeCloseFile(HANDLE hFile)
	{
		std::lock_guard<std::mutex> guard(g_lock);
		FakeFile *lpFile = Find(hFile);
		if (!lpFile)
			return FALSE;

		lpFile->nCloseCount++;
		return TRUE;
	}

	BOOL WINAPI FakeReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nBytes, LPDWORD lpnRead, LPOVERLAPPED)
	{
		std::lock_guard<std::mutex> guard(g_lock);
		FakeFile *lpFile = Find(hFile);
		if (!lpFile || lpFile->nCloseCount)
		{
			SetLastError(ERROR_INVALID_HANDLE);
			return FALSE;
		}

		// Like Storm: fail with ERROR_HANDLE_EOF on a short read, but report what was read
		size_t nAvailable = lpFile->contents.size() - lpFile->nPosition;
		DWORD nRead = (DWORD)(nBytes < nAvailable ? nBytes : nAvailable);
		memcpy(lpBuffer, lpFile->contents.data() + lpFile->nPosition, nRead);
		lpFile->nPosition += nRead;
		*lpnRead = nRead;

		if (nRead < nBytes)
		{
			SetLastError(ERROR_HANDLE_EOF);
			return FALSE;
		}

		return TRUE;
	}

	DWORD WINAPI FakeGetFileSize(HANDLE hFile, LPDWORD lpnSizeHigh)
	{
		std::lock_guard<std::mutex> guard(g_lock);
		FakeFile *lpFile = Find(hFile);
		if (!lpFile)
			return INVALID_FILE_SIZE;

		if (lpnSizeHigh)
			*lpnSizeHigh = 0;
		return (DWORD)lpFile->contents.size();
	}

	DWORD WINAPI FakeSetFilePointer(HANDLE hFile, long nDistance, long *lpnDistanceHigh, DWORD dwMoveMethod)
	{
		std::lock_guard<std::mutex> guard(g_lock);
		FakeFile *lpFile = Find(hFile);
		if (!lpFile || lpnDistanceHigh)
			return INVALID_SET_FILE_POINTER;

		long long nBase = 0;
		if (dwMoveMethod == FILE_CURRENT)
			nBase = lpFile->nPosition;
		else if (dwMoveMethod == FILE_END)
			nBase = lpFile->contents.size();

		long long nNewPosition = nBase + nDistance;
		if (nNewPosition < 0)
			return INVALID_SET_FILE_POINTER;
		if ((size_t)nNewPosition > lpFile->contents.size())
			nNewPosition = lpFile->contents.size();

		lpFile->nPosition = (size_t)nNewPosition;
		return (DWORD)nNewPosition;
	}
}

namespace FakeStorm
{
	void Reset()
	{
		std::lock_guard<std::mutex> guard(g_lock);
		g_files.clear();
	}

	HANDLE AddFile(const std::string &contents)
	{
		std::lock_guard<std::mutex> guard(g_lock);
		HANDLE hFile = (HANDLE)g_nNextHandle;
		g_nNextHandle += 4;

		g_files[hFile].contents = contents;
		return hFile;
	}

	bool IsClosed(HANDLE hFile)
	{
		return CloseCount(hFile) > 0;
	}

	int CloseCount(HANDLE hFile)
	{
		std::lock_guard<std::mutex> guard(g_lock);
		FakeFile *lpFile = Find(hFile);
		return lpFile ? lpFile->nCloseCount : -1;
	}

	STORMFILEFUNCTIONS Functions()
	{
		STORMFILEFUNCTIONS storm;
		storm.SFileCloseFile = FakeCloseFile;
		storm.SFileReadFile = FakeReadFile;
		storm.SFileGetFileSize = FakeGetFileSize;
		storm.SFileSetFilePointer = FakeSetFilePointer;
		return storm;
	}
}
