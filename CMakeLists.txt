cmake_minimum_required(VERSION 3.20)

project(ThunderGraft LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Put every executable and DLL in one directory, so test programs find the
# plugin and the fake Storm.dll next to themselves.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(TG_SOURCE_DIR ${CMAKE_SOURCE_DIR}/MPQDraft/ThunderGraft)
set(TG_COMMON_DIR ${CMAKE_SOURCE_DIR}/Common)

if(WIN32)
	enable_language(RC)

	# Settings shared by the plugin and the tests that link its code
	add_library(thundergraft_options INTERFACE)
	target_include_directories(thundergraft_options INTERFACE ${TG_COMMON_DIR} ${TG_SOURCE_DIR})
	target_compile_definitions(thundergraft_options INTERFACE
		# Target Windows XP, so that APIs from later versions can't be used by
		# accident. mingw-w64 defaults to Windows 10.
		_WIN32_WINNT=0x0501
		WINVER=0x0501
		NOMINMAX
		$<$<CONFIG:Debug>:_DEBUG>
	)
	target_compile_options(thundergraft_options INTERFACE -Wall -Wextra)

	# Everything except the plugin entry points, so the tests can link it
	add_library(thundergraft_core STATIC
		${TG_COMMON_DIR}/QDebug.cpp
		${TG_COMMON_DIR}/QHookAPI.cpp
		${TG_SOURCE_DIR}/DdaPlayer.cpp
	)
	target_link_libraries(thundergraft_core PUBLIC thundergraft_options)

	add_library(ThunderGraft SHARED
		${TG_SOURCE_DIR}/ThunderGraft.cpp
		${TG_SOURCE_DIR}/ThunderGraftPlugin.cpp
		${TG_SOURCE_DIR}/NullAudioEngine.cpp
		${TG_SOURCE_DIR}/ThunderGraft.rc
		${TG_COMMON_DIR}/MPQDPluginTemplate.def
	)
	set_target_properties(ThunderGraft PROPERTIES
		PREFIX ""
		SUFFIX ".qdp"
		OUTPUT_NAME_DEBUG ThunderGraftD
	)
	target_link_libraries(ThunderGraft PRIVATE thundergraft_core psapi)
	target_link_options(ThunderGraft PRIVATE
		# The plugin is injected into games and extracted from SEMPQs on its
		# own, so it must not depend on the MinGW runtime DLLs.
		-static
		-static-libgcc
		-static-libstdc++
		# Export GetMPQDraftPlugin without the @4 stdcall decoration
		-Wl,--kill-at
		# The .def file names it without the decoration; resolve it quietly
		-Wl,--enable-stdcall-fixup
		$<$<NOT:$<CONFIG:Debug>>:-s>
	)
endif()
