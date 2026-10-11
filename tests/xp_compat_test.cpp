// Tests for the GetThreadId replacement that keeps the plugin loadable on Windows XP. This test program links XpCompat.cpp the way the plugin does.

#include <doctest.h>

#include <windows.h>

#include <QHookAPI.h>

#include "XpCompat.h"

namespace
{
	DWORD WINAPI WaitForEvent(LPVOID lpEvent)
	{
		WaitForSingleObject((HANDLE)lpEvent, INFINITE);
		return 0;
	}
}

TEST_CASE("The fallback gets thread IDs the way GetThreadId does")
{
	CHECK(XpCompatGetThreadIdFallback(GetCurrentThread()) == GetCurrentThreadId());

	SUBCASE("Of other threads")
	{
		HANDLE hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		DWORD nThreadID = 0;
		HANDLE hThread = CreateThread(NULL, 0, WaitForEvent, hEvent, 0, &nThreadID);
		REQUIRE(hThread);

		CHECK(XpCompatGetThreadIdFallback(hThread) == nThreadID);

		SetEvent(hEvent);
		WaitForSingleObject(hThread, INFINITE);
		CloseHandle(hThread);
		CloseHandle(hEvent);
	}

	SUBCASE("Invalid handles give 0")
	{
		CHECK(XpCompatGetThreadIdFallback(NULL) == 0);
		CHECK(GetLastError() == ERROR_INVALID_HANDLE);
	}
}

TEST_CASE("The replacement stands in for kernel32's GetThreadId")
{
	REQUIRE(XpCompatGetThreadIdImport);
	CHECK(XpCompatGetThreadIdImport(GetCurrentThread()) == GetCurrentThreadId());

	// GetThreadId isn't imported from kernel32, even though the GCC runtime uses it
	ModuleFunctionMap moduleMap;
	CHECK(GetImportEntryPatches(GetModuleHandle(NULL), GetModuleHandleA("kernel32.dll"), "GetThreadId", 0, &moduleMap, FALSE) == 0);
}
