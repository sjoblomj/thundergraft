cmake_minimum_required(VERSION 3.20)

# Checks the built plugin DLL with objdump:
# - GetMPQDraftPlugin is exported under that exact name, at ordinal 100
# - it imports only from an allowlist of DLLs that ship with every Windows
#   version we support, so it can be dropped into a game directory or an
#   SEMPQ on its own
# - it doesn't import kernel32 functions that Windows XP lacks; Windows
#   refuses to load a DLL whose imports it can't resolve. The list is of
#   functions added in Vista or later that the GCC runtime or miniaudio
#   might plausibly use; it is not exhaustive.
#
# Usage: cmake -DOBJDUMP=<objdump> -DPLUGIN=<plugin file> -P check_plugin_dll.cmake

set(ALLOWED_IMPORTS kernel32.dll user32.dll msvcrt.dll psapi.dll advapi32.dll)

set(POST_XP_KERNEL32_FUNCTIONS
	GetThreadId GetProcessIdOfThread GetTickCount64 GetCurrentProcessorNumber
	InitializeSRWLock AcquireSRWLockExclusive AcquireSRWLockShared
	ReleaseSRWLockExclusive ReleaseSRWLockShared
	TryAcquireSRWLockExclusive TryAcquireSRWLockShared
	InitializeConditionVariable SleepConditionVariableCS SleepConditionVariableSRW
	WakeConditionVariable WakeAllConditionVariable
	InitOnceExecuteOnce InitOnceBeginInitialize InitOnceComplete
	InitializeCriticalSectionEx
	CreateEventExA CreateEventExW CreateMutexExA CreateMutexExW
	CreateSemaphoreExA CreateSemaphoreExW CreateWaitableTimerExW
	FlsAlloc FlsFree FlsGetValue FlsSetValue
	GetFileInformationByHandleEx SetFileInformationByHandle
	QueryFullProcessImageNameA QueryFullProcessImageNameW
	GetLocaleInfoEx CompareStringEx LCMapStringEx GetUserDefaultLocaleName
	GetErrorMode SetThreadErrorMode GetThreadErrorMode
	K32GetModuleInformation K32EnumProcessModules K32GetModuleFileNameExA
)

execute_process(
	COMMAND ${OBJDUMP} -p ${PLUGIN}
	OUTPUT_VARIABLE dump
	RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
	message(FATAL_ERROR "objdump failed on ${PLUGIN}")
endif()

# Exports
if(NOT dump MATCHES "Ordinal Base 100\n")
	message(FATAL_ERROR "Exports don't start at ordinal 100:\n${dump}")
endif()
string(REGEX MATCHALL "\\[ *[0-9]+\\] [^\n]+" exported_names "${dump}")
set(found_export FALSE)
foreach(entry IN LISTS exported_names)
	if(entry MATCHES "\\] GetMPQDraftPlugin$")
		set(found_export TRUE)
	elseif(entry MATCHES "GetMPQDraftPlugin")
		message(FATAL_ERROR "GetMPQDraftPlugin is exported with a decorated name: ${entry}")
	endif()
endforeach()
if(NOT found_export)
	message(FATAL_ERROR "GetMPQDraftPlugin is not exported by name")
endif()

# Imports
string(REGEX MATCHALL "DLL Name: [^\n]+" imports "${dump}")
foreach(import IN LISTS imports)
	string(REPLACE "DLL Name: " "" dll "${import}")
	string(STRIP "${dll}" dll)
	string(TOLOWER "${dll}" dll_lower)
	if(NOT dll_lower IN_LIST ALLOWED_IMPORTS)
		message(FATAL_ERROR "The plugin imports ${dll}, which is not on the allowlist (${ALLOWED_IMPORTS})")
	endif()
	message(STATUS "Imports ${dll}")
endforeach()

# Functions imported from kernel32: the lines after its "DLL Name" line, up to the next DLL
string(REGEX MATCH "DLL Name: KERNEL32.dll\n(.*)" kernel32_section "${dump}")
string(REGEX REPLACE "\n[^\n]*DLL Name:.*" "" kernel32_section "${CMAKE_MATCH_1}")
string(REGEX MATCHALL "\n\t[0-9a-f]+[ \t]+[0-9]+[ \t]+[A-Za-z0-9_]+" kernel32_entries "\n${kernel32_section}")
set(kernel32_count 0)
foreach(entry IN LISTS kernel32_entries)
	string(REGEX REPLACE ".* ([A-Za-z0-9_]+)$" "\\1" function "${entry}")
	math(EXPR kernel32_count "${kernel32_count} + 1")
	if(function IN_LIST POST_XP_KERNEL32_FUNCTIONS)
		message(FATAL_ERROR "The plugin imports ${function} from kernel32.dll, which Windows XP doesn't have")
	endif()
endforeach()
if(kernel32_count EQUAL 0)
	message(FATAL_ERROR "Couldn't find the kernel32.dll imports in the objdump output")
endif()
message(STATUS "Checked ${kernel32_count} kernel32.dll imports")
