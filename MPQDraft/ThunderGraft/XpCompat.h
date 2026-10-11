/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	Keeps the plugin loadable on Windows XP.

	The GCC runtime that is linked into the plugin imports GetThreadId from kernel32.dll, which only exists from Windows Vista on; a DLL importing a function Windows doesn't have fails to load. XpCompat.cpp defines that import itself, so the linker doesn't take it from kernel32: it uses GetThreadId where it exists, and falls back to NtQueryInformationThread (which XP has) where it doesn't.

	XpCompat.cpp must be linked as an object file of the plugin, not from a static library, or the linker would pick kernel32's definition first.
*/

#ifndef XPCOMPAT_H
#define XPCOMPAT_H

#include <windows.h>

// The fallback used where kernel32 has no GetThreadId. Exposed for the tests.
DWORD WINAPI XpCompatGetThreadIdFallback(IN HANDLE hThread);

// The import table entry the GCC runtime calls GetThreadId through, which XpCompat.cpp defines instead of kernel32
extern "C" DWORD (WINAPI *XpCompatGetThreadIdImport)(HANDLE) __asm__("__imp__GetThreadId@4");

#endif // #ifndef XPCOMPAT_H
