/* A DLL whose exports the import-patching tests redirect: one exported by name, one by ordinal only */
#include <windows.h>

int WINAPI HookTargetAdd(int a, int b)
{
	return a + b;
}

int WINAPI HookTargetByOrdinal(int a, int b)
{
	return a - b;
}
