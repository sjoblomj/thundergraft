/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

#include "XpCompat.h"

namespace
{
	// The start of THREAD_BASIC_INFORMATION, from the Windows DDK
	struct THREADBASICINFO
	{
		LONG ExitStatus;
		PVOID TebBaseAddress;
		HANDLE UniqueProcess;
		HANDLE UniqueThread;
		ULONG_PTR AffinityMask;
		LONG Priority;
		LONG BasePriority;
	};

	const int THREAD_BASIC_INFORMATION_CLASS = 0;

	typedef LONG (WINAPI *NtQueryInformationThreadPtr)(HANDLE hThread, int nInfoClass, PVOID lpInfo, ULONG nInfoLength, PULONG lpnReturnLength);
	typedef DWORD (WINAPI *GetThreadIdPtr)(HANDLE hThread);

	// Looked up on first use. Deliberately not function-local statics: their thread-safe initialization would itself use the GCC threading code that calls GetThreadId. Racing threads simply look the function up twice.
	FARPROC g_pfnGetThreadId = NULL;
	BOOL g_bGetThreadIdLookedUp = FALSE;

	DWORD WINAPI GetThreadIdCompat(HANDLE hThread)
	{
		if (!g_bGetThreadIdLookedUp)
		{
			g_pfnGetThreadId = ::GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetThreadId");
			g_bGetThreadIdLookedUp = TRUE;
		}

		if (g_pfnGetThreadId)
			return ((GetThreadIdPtr)(void (*)(void))g_pfnGetThreadId)(hThread);

		return XpCompatGetThreadIdFallback(hThread);
	}
}

DWORD WINAPI XpCompatGetThreadIdFallback(IN HANDLE hThread)
{
	HMODULE hNtDll = GetModuleHandleA("ntdll.dll");
	NtQueryInformationThreadPtr pfnQuery = hNtDll ? (NtQueryInformationThreadPtr)(void (*)(void))::GetProcAddress(hNtDll, "NtQueryInformationThread") : NULL;

	THREADBASICINFO info;
	if (!pfnQuery || pfnQuery(hThread, THREAD_BASIC_INFORMATION_CLASS, &info, sizeof(info), NULL) < 0)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return 0;
	}

	return (DWORD)(ULONG_PTR)info.UniqueThread;
}

// Defining the import table entry here keeps the linker from importing GetThreadId from kernel32
DWORD (WINAPI *XpCompatGetThreadIdImport)(HANDLE) = GetThreadIdCompat;
