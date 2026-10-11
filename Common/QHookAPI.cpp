/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2007 Justin Olbrantz. All Rights Reserved.
*/

#include <QHookAPI.h>
#include <winnt.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>

// Gets the PE header of a loaded module, after checking that the module looks
// like a valid image for this address space. Returns NULL if it does not.
static PIMAGE_NT_HEADERS GetImageNtHeaders(IN HMODULE hModule)
{
	assert(hModule);

	// The HMODULE is actually a pointer to the image's IMAGE_DOS_HEADER
	PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)hModule;

	// Check if it looks like a DOS header, and pass up the DOS header to 
	// get to the Win32 (PE) header. The offset to the PE header must leave
	// room for the PE header itself inside the first page of the image, which
	// is always mapped.
	if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE
		|| pDosHeader->e_lfanew < (LONG)sizeof(IMAGE_DOS_HEADER)
		|| pDosHeader->e_lfanew > 0x1000 - (LONG)sizeof(IMAGE_NT_HEADERS))
		return NULL;

	// The DOS header contains a field that indicates the offset to the
	// Windows PE header.
	PIMAGE_NT_HEADERS pNTHeader = (PIMAGE_NT_HEADERS)
		((LPBYTE)pDosHeader + pDosHeader->e_lfanew);

	// Verify that it's the proper file format for this address space
	if (pNTHeader->Signature != IMAGE_NT_SIGNATURE
		|| pNTHeader->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC)
		return NULL;

	return pNTHeader;
}

// Checks that nSize bytes starting at the relative virtual address nRVA lie
// within the image. This is what keeps us from wandering off into unmapped
// memory when a module's headers are damaged or not what we expect.
static BOOL IsRangeInImage(IN HMODULE hModule, IN DWORD nRVA, IN DWORD nSize)
{
	PIMAGE_NT_HEADERS pNTHeader = GetImageNtHeaders(hModule);
	if (!pNTHeader)
		return FALSE;

	DWORD nImageSize = pNTHeader->OptionalHeader.SizeOfImage;
	return nRVA < nImageSize && nSize <= nImageSize - nRVA;
}

// Checks that the null-terminated string at the relative virtual address nRVA
// lies entirely within the image.
static BOOL IsStringInImage(IN HMODULE hModule, IN DWORD nRVA)
{
	PIMAGE_NT_HEADERS pNTHeader = GetImageNtHeaders(hModule);
	if (!pNTHeader)
		return FALSE;

	DWORD nImageSize = pNTHeader->OptionalHeader.SizeOfImage;
	for (DWORD iChar = nRVA; iChar < nImageSize; iChar++)
	{
		if (!((LPBYTE)hModule)[iChar])
			return TRUE;
	}

	return FALSE;
}

// Get a given data directory entry from an HMODULE. The data directory contains entries for important sections in the module, such as the import and export tables. This requires a bit of parsing of the PE file format - just enough to determine that it's a PE file and find its data directory.
BOOL FindDataDirectoryEntry(
	// The module to find the desired data directory in
	IN HMODULE hModule,
	// The index of the desired data directory. See winnt.h for the list.
	IN DWORD iEntryIndex, 
	// The pointer to the data directory
	OUT PVOID *lplpDirEntry, 
	// The size of the data directory
	OUT OPTIONAL LPDWORD lpnEntrySize)
{
	assert(hModule);
	assert(lplpDirEntry);

	PIMAGE_NT_HEADERS pNTHeader = GetImageNtHeaders(hModule);
	if (!pNTHeader)
		return FALSE;

	// It's possible the directory entry in the table doesn't exist. The table
	// is variable size, to allow new entries to be created later.
	if (iEntryIndex >= pNTHeader->OptionalHeader.NumberOfRvaAndSizes
		|| iEntryIndex >= IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
		return FALSE;

	// It's also possible that the entry in the table is empty
	IMAGE_DATA_DIRECTORY &dirEntry = pNTHeader->OptionalHeader.DataDirectory[iEntryIndex];
	if (!dirEntry.Size || !dirEntry.VirtualAddress)
		return FALSE;

	// Or that it's damaged
	if (!IsRangeInImage(hModule, dirEntry.VirtualAddress, dirEntry.Size))
		return FALSE;

	// Return the info that is requested
	*lplpDirEntry = (PVOID)((LPBYTE)hModule + dirEntry.VirtualAddress);
	if (lpnEntrySize)
		*lpnEntrySize = dirEntry.Size;

	return TRUE;
}

// Checks that an import descriptor and the tables it refers to lie within the
// image. Does not check the individual names imported by name.
static BOOL IsImportDescriptorValid(IN HMODULE hModule, IN const IMAGE_IMPORT_DESCRIPTOR &importDesc)
{
	return IsStringInImage(hModule, importDesc.Name)
		&& IsRangeInImage(hModule, importDesc.FirstThunk, sizeof(FARPROC))
		&& (!importDesc.OriginalFirstThunk 
			|| IsRangeInImage(hModule, importDesc.OriginalFirstThunk, sizeof(IMAGE_THUNK_DATA)));
}

// Does the actual patching. The public functions merely set up some of the more peculiar parameters to this function before calling it. Also, this function may execute recursively, depending on whether we want to patch the import entry in all modules, or just a specific one. In the case of the former, we expect to have this function called for the EXE itself, and any DLLs that get loaded; this should account for all modules in the process. Returns the number of import table entries replaced, or (DWORD)-1 on failure.
DWORD PatchImportCore(
	// The module whose imports we are patching
	IN HMODULE hHostProgram, 
	// The module exporting the function we are replacing in the import table. The reason this is needed is that there are many forms the module name may appear as in the import table, and we want to catch all of them that refer to the module exporting the function we're going to patch. So I figured why not just call GetModuleHandle on the string in the import table, and let Windows do the comparison for us?
	IN HMODULE hExportModule, 
	// The function we're going to patch in the import table
	IN FARPROC pfnOldFunction, 
	// The new function we're going to redirect the target to
	IN FARPROC pfnNewFunction, 
	// Whether or not this function should call itself recursively to patch all modules imported by the one it was originally supposed to patch
	IN BOOL bRecurse, 
	// A set of modules to exclude from patching. If the target module is in this list, this function immediately returns success. If not, the module is added to this list. If this is NULL (not recommended), all modules are patched.
	IN OUT OPTIONAL ModuleSet *pModules
	)
{
	assert(hHostProgram);
	assert(hExportModule);
	assert(pfnOldFunction);
	assert(pfnNewFunction);

	// First we need to make sure we're not getting called to patch a
	// module we've already done. So check the list of modules already patched.
	// If we haven't already patched it, add it to the module list and patch it.
	if (pModules)
	{
		ModuleSet::iterator itModule = pModules->find(hHostProgram);
		if (itModule != pModules->end())
			return 0;	// Been there, done that

		try
		{ pModules->insert(hHostProgram); }
		catch (...)
		{ return (DWORD)-1; }
	}

	// Find the import table for the module
	PIMAGE_IMPORT_DESCRIPTOR pImportDesc;
	DWORD nImportSize;
	if (!FindDataDirectoryEntry(hHostProgram, IMAGE_DIRECTORY_ENTRY_IMPORT, (PVOID *)&pImportDesc, &nImportSize))
		return 0;

	// The import table entries will be arranged according to the module that
	// they're imported from. So first we need to find the right module in the
	// import table.
	DWORD nPatchCount = 0;	// Number of patches made in this module and children

	DWORD nMaxDescriptors = nImportSize / sizeof(IMAGE_IMPORT_DESCRIPTOR);
	for (DWORD iDescIndex = 0; iDescIndex < nMaxDescriptors && pImportDesc[iDescIndex].Name; iDescIndex++)
	{
		if (!IsImportDescriptorValid(hHostProgram, pImportDesc[iDescIndex]))
			return (DWORD)-1;

		LPCSTR lpszImportName = (LPCSTR)
			((LPBYTE)hHostProgram + pImportDesc[iDescIndex].Name);
		HMODULE hChildModule = GetModuleHandle(lpszImportName);

		// A module that isn't loaded can't be the one we're looking for, and
		// it has no import table of its own for us to recurse into.
		if (hChildModule == NULL)
			continue;

		// Is this the module we're looking for?
		if (hChildModule != hExportModule)
		{
			// No. But if we're supposed to execute recursively, we need to patch
			// this module, as well.
			if (bRecurse)
			{
				DWORD nPatchesMade = PatchImportCore(hChildModule, hExportModule, 
					pfnOldFunction, pfnNewFunction, bRecurse, pModules);
				if (nPatchesMade == (DWORD)-1)
					return (DWORD)-1;
				
				nPatchCount += nPatchesMade;
			}

			continue;
		}

		// Now that we've found the right module, there will be a table of straight
		// pointers to each of the functions imported from the module. 
		// Technically these are IMAGE_THUNK_DATA entries, but for the purpose 
		// of maintaining a single code branch for both 32-bit and 64-bit address
		// spaces, considering it a table of FARPROCs works much better.
		FARPROC *pThunk = (FARPROC *)
			((LPBYTE)hHostProgram + pImportDesc[iDescIndex].FirstThunk);

		// Search through the whole table of imported functions
		for (DWORD iThunkIndex = 0; pThunk[iThunkIndex]; iThunkIndex++)
		{
			// Make sure the next entry is still inside the image
			if (!IsRangeInImage(hHostProgram, pImportDesc[iDescIndex].FirstThunk 
				+ (iThunkIndex + 1) * sizeof(FARPROC), sizeof(FARPROC)))
				return (DWORD)-1;

			// See if this is the function we're looking for
			if (pThunk[iThunkIndex] != pfnOldFunction)
				continue;	// Nope

			DWORD dwOldProtection, dwUnused;

			// If it is the one, we'll replace it. Usually the import table
			// will be write-protected, so we'll have to anull that before we
			// tweak the table. VirtualProtect is the function we want.
			if (!VirtualProtect(&pThunk[iThunkIndex], sizeof(pThunk[iThunkIndex]), PAGE_READWRITE, &dwOldProtection))
				return (DWORD)-1;

			pThunk[iThunkIndex] = pfnNewFunction;

			VirtualProtect(&pThunk[iThunkIndex], sizeof(pThunk[iThunkIndex]), dwOldProtection, &dwUnused);

			nPatchCount++;

			// While a module cannot link to the same function in an imported
			// module twice, there are two occasions when we might see the same
			// function address twice: when two or more exports are aliases to
			// the same function, and when something else has already hooked 
			// some of the functions. In either case, we can't stop here.
		}
	}

	return nPatchCount;
}

// This is actually just a thin wrapper around PatchImportCore. All it does is 
// get the HMODULE from the exporting module name, and then call 
// PatchImportCore.
DWORD WINAPI PatchImportEntry(
	IN HMODULE hHostProgram, 
	IN LPCSTR lpszModuleName, 
	IN FARPROC pfnOldFunction, 
	IN FARPROC pfnNewFunction, 
	IN OUT ModuleSet *lpModuleSet, 
	IN BOOL bRecurse)
{
	assert(lpszModuleName);
	assert(lpModuleSet);

	// Look up the module that is being imported from. This seems really weird,
	// but it was the most reliable way I could think of of checking whether a
	// module being imported from is equal to a module that we've already
	// patched, as there can be many file name forms in the import table (or 
	// specified by the caller, for that matter).
	HMODULE hExportModule = GetModuleHandle(lpszModuleName);
	if (!hExportModule)
		return (DWORD)-1;

	// Do the patching itself. Damaged headers are caught by the checks in
	// PatchImportCore, which return failure rather than crashing.
	return PatchImportCore(hHostProgram, hExportModule, 
		pfnOldFunction, pfnNewFunction, bRecurse, lpModuleSet);
}

DWORD PatchImportCore(
	IN HMODULE hHostProgram,
	IN HMODULE hExportModule,
	IN OPTIONAL LPCSTR lpszFunctionName,
	IN OPTIONAL DWORD nFunctionOrdinal,
	IN OPTIONAL FARPROC pfnNewFunction,
	IN BOOL bRecurse,
	IN OUT OPTIONAL ModuleSet *lpModuleSet,
	IN OUT OPTIONAL ModuleFunctionMap *lpModuleMap
)
{
	assert(hHostProgram);
	assert(hExportModule);
	assert(lpszFunctionName || nFunctionOrdinal);
	assert(lpModuleSet);

	// First we need to make sure we're not getting called to patch a
	// module we've already done. So check the list of modules already patched.
	// If we haven't already patched it, add it to the module list and patch it.
	if (lpModuleSet)
	{
		ModuleSet::iterator itModule = lpModuleSet->find(hHostProgram);
		if (itModule != lpModuleSet->end())
			return 0;	// Been there, done that

		try
		{ lpModuleSet->insert(hHostProgram); }
		catch (...)
		{ return (DWORD)-1; }
	}

	// Find the import table for the module
	PIMAGE_IMPORT_DESCRIPTOR pImportDesc;
	DWORD nImportSize;
	if (!FindDataDirectoryEntry(hHostProgram, IMAGE_DIRECTORY_ENTRY_IMPORT, (PVOID *)&pImportDesc, &nImportSize))
		return 0;

	// The import table entries will be arranged according to the module that
	// they're imported from. So first we need to find the right module in the
	// import table.
	DWORD nPatchCount = 0;	// Number of patches made in this module and children

	DWORD nMaxDescriptors = nImportSize / sizeof(IMAGE_IMPORT_DESCRIPTOR);
	for (DWORD iDescIndex = 0; iDescIndex < nMaxDescriptors && pImportDesc[iDescIndex].Name; iDescIndex++)
	{
		if (!IsImportDescriptorValid(hHostProgram, pImportDesc[iDescIndex]))
			return (DWORD)-1;

		LPCSTR lpszImportName = (LPCSTR)
			((LPBYTE)hHostProgram + pImportDesc[iDescIndex].Name);
		HMODULE hChildModule = GetModuleHandle(lpszImportName);

		// A module that isn't loaded can't be the one we're looking for, and
		// it has no import table of its own for us to recurse into.
		if (hChildModule == NULL)
			continue;

		// Is this the module we're looking for?
		if (hChildModule != hExportModule)
		{
			// No. But if we're supposed to execute recursively, we need to patch
			// this module, as well.
			if (bRecurse)
			{
				DWORD nPatchesMade = PatchImportCore(hChildModule, hExportModule, 
					lpszFunctionName, nFunctionOrdinal, pfnNewFunction, bRecurse, 
					lpModuleSet, lpModuleMap);
				if (nPatchesMade == (DWORD)-1)
					return (DWORD)-1;
				
				nPatchCount += nPatchesMade;
			}

			continue;
		}

		// Now that we've found the right module, there will be a table of straight
		// pointers to each of the functions imported from the module, as well 
		// as a second table that contains the information about how the 
		// function is imported; however, this table, while common, is optional.
		// In reality, in the file on disk, these two tables are identical, 
		// which is why one can be optional; the mandatory one gets overwritten 
		// by the loader and turned into the table of function pointers.
		if (!pImportDesc[iDescIndex].OriginalFirstThunk)
			// Technically this is an error, as there's no wa for us to perform
			// our patching without the original first thunk table. However, 
			// between the available options, failing silently seems the best.
			continue;

		// Technically these are IMAGE_THUNK_DATA entries, but for the purpose 
		// of maintaining a single code branch for both 32-bit and 64-bit address
		// spaces, considering it a table of FARPROCs works much better.
		FARPROC *pThunk = (FARPROC *)
			((LPBYTE)hHostProgram + pImportDesc[iDescIndex].FirstThunk);
		PIMAGE_THUNK_DATA pOrigThunk = (PIMAGE_THUNK_DATA)
			((LPBYTE)hHostProgram + pImportDesc[iDescIndex].OriginalFirstThunk);

		// Search through the whole table of imported functions
		// Is it imported by name or by ordinal?
		for (DWORD iThunkIndex = 0; pThunk[iThunkIndex]; iThunkIndex++)
		{
			// Make sure the next entries are still inside the image
			if (!IsRangeInImage(hHostProgram, pImportDesc[iDescIndex].FirstThunk 
					+ (iThunkIndex + 1) * sizeof(FARPROC), sizeof(FARPROC))
				|| !IsRangeInImage(hHostProgram, pImportDesc[iDescIndex].OriginalFirstThunk 
					+ (iThunkIndex + 1) * sizeof(IMAGE_THUNK_DATA), sizeof(IMAGE_THUNK_DATA)))
				return (DWORD)-1;

			// See if this is the function we're looking for
			if (IMAGE_SNAP_BY_ORDINAL(pOrigThunk[iThunkIndex].u1.Ordinal))
			{
				// It's imported by ordinal
				if (!nFunctionOrdinal || nFunctionOrdinal 
					!= IMAGE_ORDINAL(pOrigThunk[iThunkIndex].u1.Ordinal))
					continue;
			}
			else
			{
				// It's imported by name
				if (!IsStringInImage(hHostProgram, (DWORD)pOrigThunk[iThunkIndex].u1.AddressOfData 
					+ offsetof(IMAGE_IMPORT_BY_NAME, Name)))
					return (DWORD)-1;

				PIMAGE_IMPORT_BY_NAME pThunkName = (PIMAGE_IMPORT_BY_NAME)
					((LPBYTE)hHostProgram + pOrigThunk[iThunkIndex].u1.AddressOfData);

				if (!lpszFunctionName || 
					strcmp(lpszFunctionName, (LPCSTR)pThunkName->Name) != 0)
					continue;
			}

			// If we have a patch list, add this to the list
			if (lpModuleMap)
			{
				try
				{
					lpModuleMap->insert(ModuleFunctionMap::value_type(
						hHostProgram, pThunk[iThunkIndex]));
				}
				catch (...)
				{ return (DWORD)-1; }
			}

			// If we're supposed to patch it, do so
			if (pfnNewFunction)
			{
				DWORD dwOldProtection, dwUnused;

				// If it is the one, we'll replace it. Usually the import table
				// will be write-protected, so we'll have to anull that before we
				// tweak the table. VirtualProtect is the function we want.
				if (!VirtualProtect(&pThunk[iThunkIndex], sizeof(pThunk[iThunkIndex]), PAGE_READWRITE, &dwOldProtection))
					return (DWORD)-1;

				pThunk[iThunkIndex] = pfnNewFunction;

				VirtualProtect(&pThunk[iThunkIndex], sizeof(pThunk[iThunkIndex]), dwOldProtection, &dwUnused);
			}

			nPatchCount++;

			// Shouldn't be possible to import the same function twice
			break;
		}
	}

	return nPatchCount;
}

DWORD WINAPI PatchImportEntry(
	IN HMODULE hHostProgram,
	IN HMODULE hExportModule,
	IN OPTIONAL LPCSTR lpszFunctionName,
	IN OPTIONAL DWORD nFunctionOrdinal,
	IN OPTIONAL FARPROC pfnNewFunction,
	IN OUT ModuleSet *lpModuleSet,
	IN OUT OPTIONAL ModuleFunctionMap *lpModuleMap,
	IN OPTIONAL BOOL bRecurse
)
{
	assert(hHostProgram);
	assert(hExportModule);
	assert(lpszFunctionName || nFunctionOrdinal);
	assert(lpModuleSet);

	if (lpModuleMap)
		lpModuleMap->clear();

	// Do the patching itself. Damaged headers are caught by the checks in
	// PatchImportCore, which return failure rather than crashing.
	return PatchImportCore(hHostProgram, hExportModule, 
		lpszFunctionName, nFunctionOrdinal, pfnNewFunction, bRecurse, 
		lpModuleSet, lpModuleMap);
}
