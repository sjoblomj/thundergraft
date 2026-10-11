cmake_minimum_required(VERSION 3.20)

cmake_minimum_required(VERSION 3.20)

# Checks the built plugin DLL with objdump:
# - GetMPQDraftPlugin is exported under that exact name, at ordinal 100
# - it imports only from an allowlist of DLLs that ship with every Windows
#   version we support, so it can be dropped into a game directory or an
#   SEMPQ on its own
#
# Usage: cmake -DOBJDUMP=<objdump> -DPLUGIN=<plugin file> -P check_plugin_dll.cmake

set(ALLOWED_IMPORTS kernel32.dll user32.dll msvcrt.dll psapi.dll)

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
