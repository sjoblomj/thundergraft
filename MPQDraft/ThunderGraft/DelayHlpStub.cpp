#include "stdafx.h"

#include <delayhlp.cpp>

// The function to load delay-loaded DLLs. We need our own copy of it because there's a bug in the one supplied with VC++; namely, that it uses memcmp instead of stricmp to match the filename.
HRESULT WINAPI
__HrLoadAllImportsForDll2(LPCSTR szDll) {
    HRESULT             hrRet = HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    PIMAGE_NT_HEADERS   pinh = PinhFromImageBase(HMODULE(&__ImageBase));

    // Scan the Delay load IAT/INT for the dll in question
    //
    if (pinh->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size) {
        PCImgDelayDescr     pidd;

        pidd = PFromRva<PCImgDelayDescr>(
            pinh->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress
            );

        // Check all of the dlls listed up to the NULL one.
        //
        while (pidd->rvaDLLName) {
            // Check to see if it is the DLL we want to load.
            // Intentionally case sensitive to avoid complication of using the CRT
            // for those that don't use the CRT...the user can replace this with
            // a variant of a case insenstive comparison routine.
            //
            LPCSTR  szDllCur = PFromRva<LPCSTR>(pidd->rvaDLLName);
            size_t  cchDllCur = __strlen(szDllCur);
            if (cchDllCur == __strlen(szDll) && _stricmp(szDll, szDllCur) == 0) {
                // We found it, so break out with pidd and szDllCur set appropriately
                //
                break;
                }
            
            // Advance to the next delay import descriptor
            //
            pidd++;
            }
        
        if (pidd->rvaDLLName) {
            // Found a matching DLL name, now process it.
            //
            // Set up the internal structure
            //
            FARPROC *   ppfnIATEntry = PFromRva<FARPROC*>(pidd->rvaIAT);
            size_t      cpfnIATEntries = CountOfImports(PCImgThunkData(ppfnIATEntry));
            FARPROC *   ppfnIATEntryMax = ppfnIATEntry + cpfnIATEntries;

            for (;ppfnIATEntry < ppfnIATEntryMax; ppfnIATEntry++) {
                __delayLoadHelper2(pidd, ppfnIATEntry);
                }

            // Done, indicate some semblance of success
            //
            hrRet = S_OK;
            }
        }
    return hrRet;
}

ExternC PfnDliHook __pfnDliNotifyHook2 = NULL;
ExternC PfnDliHook __pfnDliFailureHook2 = NULL;