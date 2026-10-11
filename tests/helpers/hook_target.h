/* Imports from the test helper DLLs */
#ifndef HOOK_TARGET_H
#define HOOK_TARGET_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* From hook_target.dll */
__declspec(dllimport) int WINAPI HookTargetAdd(int a, int b);
__declspec(dllimport) int WINAPI HookTargetByOrdinal(int a, int b);

/* From hook_user.dll, which calls HookTargetAdd through its own import table */
#ifndef BUILDING_HOOK_USER
__declspec(dllimport) int WINAPI CallHookTargetAdd(int a, int b);
#endif

#ifdef __cplusplus
}
#endif

#endif
