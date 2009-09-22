/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2007 Justin Olbrantz. All Rights Reserved.
*/

#ifndef QHOOKAPI_H
#define QHOOKAPI_H

#include <windows.h>
#include <hash_set>
#include <hash_map>

// We're going to keep track of all the modules we've patched so that we don't
// do the same one and its decendants twice. This will significantly speed up
// recursive patching operations.
typedef stdext::hash_set<HMODULE> ModuleSet;

/*
	* PatchImportEntry *

	Patches a module's import table to redirect one imported function to a
	version you supply. PatchImportEntry returns the number of patches made, 
	or -1 on failure.

	The import table is a list of functions a module will call in other 
	modules, such as the Windows system DLLs. Calls made normally in the source
	will go through the import table to locate the function in a different
	module; this import table will be hash_set up by Windows when the module in
	question is loaded. GetProcAddress, however, does not use the import table.
	Rather, it goes directly to the module the desired function is in, and 
	looks at its export table, which lists functions that module makes 
	available for other modules to import. For this reason, this function will
	not alter what is returned by GetProcAddress for a function, even if the
	function had been patched by PatchImportEntry.
*/
DWORD WINAPI PatchImportEntry(
	// The (root) module whose import table is to be patched. If this call is 
	// recursive, all modules imported by this module or any other module will 
	// be patched.
	IN HMODULE hHostProgram, 
	// The name of the module which is exporting the function which is being 
	// replaced in some module(s) import table. May be in any form recognized
	// by LoadLibrary; e.g. "kernel32", "kernel32.dll", 
	// "c:\windows\system32\kernel32.dll".
	IN LPCSTR lpszModuleName, 
	// The function which is to be replaced in import tables
	IN FARPROC pfnOldFunction, 
	// The function which is to replace the old function in import tables
	IN FARPROC pfnNewFunction, 
	// A hash_set of modules that have already been patched. This function will
	// ignore any modules in this list, and add any not in the list to it.
	IN OUT ModuleSet *lpModuleSet,
	// Whether the function should execute recursively. If TRUE, this function
	// will be called (also recursively) on any modules imported by the 
	// specified module to be patched.
	IN OPTIONAL BOOL bRecurse = FALSE
);

// This version of PatchImportEntry is identical to the previous one, but 
// allocates the ModuleSet internally. This version should NOT be used when
// the calling application expects to be able to manage the list of patched
// modules itself.
inline DWORD WINAPI PatchImportEntry(
	IN HMODULE hHostProgram, 
	IN LPCSTR lpszModuleName, 
	IN FARPROC pfnOldFunction, 
	IN FARPROC pfnNewFunction, 
	IN OPTIONAL BOOL bRecurse = FALSE
)
{
	try
	{
		ModuleSet modules;

		return PatchImportEntry(hHostProgram, lpszModuleName, pfnOldFunction, pfnNewFunction, &modules, bRecurse);
	}
	catch (...)
	{ return (DWORD)-1; }
}

// When we're patching a function that may already be patched, and may be 
// patched differently in different modules, there's the possibility that there
// will be a one-to-many relationship. This map indicates the different 
// functions our target function has been patched to, and in which modules.
typedef stdext::hash_map<HMODULE, FARPROC> ModuleFunctionMap;

/*
	Searches through the import table(s) of one or more modules to for a single
	function. This function may be used to patch all occurrances, if 
	pfnNewFunction is specified, optionally returning the various function 
	addresses the function points to in the various importing modules; if 
	pfnNewFunction is NULL, it merely returns said list of addresses. In either
	case, it returns the number of import entries seen, or (DWORD)-1 on 
	failure.

	The target function may be identified, as with the import table in general,
	either by name or by ordinal (or both). When scanning the import table, if 
	the current entry is imported by name the name is compared to 
	lpszFunctionName (if specified); if it is imported by ordinal, it is 
	compared to nFunctionOrdinal (if specified). It does not verify that the 
	name and ordinal specified really refer to the same function.

	This function requires some care to use, as it can very easily be misued 
	with unwanted consequences. Because this function is not limited to 
	hooking a single target address, including import entries that have 
	already been hooked by the caller or other modules, it's very easy to 
	accidentally hook functions that have already been hook by the caller, 
	resulting in a hook appearing multiple times in the hook chain, other 
	modules that should be hooking the function getting eliminated, or other 
	degenerate cases. This is especially easy to do if the caller uses this 
	function took hook newly loaded DLLs with recursion. Beware.

	Most parameters are essentially identical to in the previous versions, and 
	they are not documented again here.
*/
DWORD WINAPI PatchImportEntry(
	IN HMODULE hHostProgram,
	// The HANDLE of the module exporting the target function
	IN HMODULE hExportModule,
	// The name of the target function, or NULL to ignore imports by name
	IN OPTIONAL LPCSTR lpszFunctionName,
	// The ordinal of the target function, or 0 to ignore imports by ordinal
	IN OPTIONAL DWORD nFunctionOrdinal,
	// The function to redirect the target to, or NULL to not redirect
	IN OPTIONAL FARPROC pfnNewFunction,
	IN OUT ModuleSet *lpModuleSet,
	// The map of modules to functions. This is purely for the purpose of 
	// returning information to the caller, and gets cleared when called.
	IN OUT OPTIONAL ModuleFunctionMap *lpModuleMap = NULL,
	IN OPTIONAL BOOL bRecurse = FALSE
);

inline DWORD WINAPI PatchImportEntry(
	IN HMODULE hHostProgram,
	IN HMODULE hExportModule,
	IN OPTIONAL LPCSTR lpszFunctionName,
	IN OPTIONAL DWORD nFunctionOrdinal,
	IN OPTIONAL FARPROC pfnNewFunction,
	IN OUT OPTIONAL ModuleFunctionMap *lpModuleMap = NULL,
	IN OPTIONAL BOOL bRecurse = FALSE
)
{
	try
	{
		ModuleSet modules;

		return PatchImportEntry(hHostProgram, hExportModule, lpszFunctionName, nFunctionOrdinal, pfnNewFunction, &modules, lpModuleMap, bRecurse);
	}
	catch (...)
	{ return (DWORD)-1; }
}

// Alias that finds all imported function addresses for a single imported 
// function. It simply calls PatchImportEntry with pfnNewFunction NULL.
inline DWORD WINAPI GetImportEntryPatches(
	IN HMODULE hHostProgram,
	IN HMODULE hExportModule,
	IN OPTIONAL LPCSTR lpszFunctionName,
	IN OPTIONAL DWORD nFunctionOrdinal,
	IN OUT ModuleFunctionMap *lpModuleMap,
	IN OPTIONAL BOOL bRecurse = FALSE
)
{
	try
	{
		ModuleSet modules;

		return PatchImportEntry(hHostProgram, hExportModule, lpszFunctionName, nFunctionOrdinal, NULL, &modules, lpModuleMap, bRecurse);
	}
	catch (...)
	{ return (DWORD)-1; }
}

#endif