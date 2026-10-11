/* A second module importing from hook_target.dll, for testing recursive patching */
#define BUILDING_HOOK_USER
#include "hook_target.h"

int WINAPI CallHookTargetAdd(int a, int b)
{
	return HookTargetAdd(a, b);
}
