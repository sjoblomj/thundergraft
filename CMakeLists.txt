cmake_minimum_required(VERSION 3.20)

project(ThunderGraft LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(THUNDERGRAFT_BUILD_TESTS "Build the tests" ON)

# The version the plugin reports to MPQDraft. Releases are built with the tag;
# without one, the plugin says it is a development build.
set(THUNDERGRAFT_VERSION "" CACHE STRING "Version the plugin reports, such as 2026.10.12.1")
# MPQDraft has room for 63 characters, including "ThunderGraft v" and " Debug Build"
string(LENGTH "${THUNDERGRAFT_VERSION}" TG_VERSION_LENGTH)
if(TG_VERSION_LENGTH GREATER 37)
	message(FATAL_ERROR "THUNDERGRAFT_VERSION is too long for MPQDraft's plugin name: ${THUNDERGRAFT_VERSION}")
endif()

# Put every executable and DLL in one directory, so test programs find the
# plugin and the fake Storm.dll next to themselves.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(TG_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src)
set(TG_COMMON_DIR ${CMAKE_SOURCE_DIR}/Common)
set(TG_THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party)

if(WIN32)
	# Code that goes into the plugin targets Windows XP, so that APIs from later
	# versions can't be used by accident. mingw-w64 defaults to Windows 10.
	# Test programs don't ship, and are free to use newer APIs (GCC's
	# std::thread needs Vista).
	set(TG_PLUGIN_DEFINITIONS _WIN32_WINNT=0x0501 WINVER=0x0501)
endif()

# miniaudio and stb_vorbis. The configuration has to be the same for
# everything that includes miniaudio.h, as it changes the size of structures.
add_library(miniaudio STATIC ${TG_SOURCE_DIR}/MiniaudioImpl.c)
target_include_directories(miniaudio SYSTEM PUBLIC ${TG_THIRD_PARTY_DIR}/miniaudio)
target_compile_definitions(miniaudio
	PUBLIC
		MA_ENABLE_ONLY_SPECIFIC_BACKENDS
		MA_ENABLE_WASAPI
		MA_ENABLE_DSOUND
		MA_ENABLE_WINMM
		# Plays nothing; for tests
		MA_ENABLE_NULL
		MA_NO_ENCODING
		MA_NO_GENERATION
	PRIVATE
		${TG_PLUGIN_DEFINITIONS}
)
if(NOT WIN32)
	find_package(Threads REQUIRED)
	target_link_libraries(miniaudio PUBLIC Threads::Threads m ${CMAKE_DL_LIBS})
endif()

# The platform-independent part of ThunderGraft: the audio engine
add_library(thundergraft_portable STATIC
	${TG_SOURCE_DIR}/MiniaudioEngine.cpp
	${TG_SOURCE_DIR}/OggLength.cpp
)
target_include_directories(thundergraft_portable PUBLIC ${TG_SOURCE_DIR})
target_link_libraries(thundergraft_portable PUBLIC miniaudio)
target_compile_definitions(thundergraft_portable PRIVATE ${TG_PLUGIN_DEFINITIONS})
target_compile_options(thundergraft_portable PRIVATE -Wall -Wextra)

if(WIN32)
	enable_language(RC)

	# Settings shared by the plugin and the tests that use its code
	add_library(thundergraft_options INTERFACE)
	target_include_directories(thundergraft_options INTERFACE ${TG_COMMON_DIR} ${TG_SOURCE_DIR})
	target_compile_definitions(thundergraft_options INTERFACE
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
	target_link_libraries(thundergraft_core PUBLIC thundergraft_options thundergraft_portable)
	target_compile_definitions(thundergraft_core PRIVATE ${TG_PLUGIN_DEFINITIONS})

	add_library(ThunderGraft SHARED
		${TG_SOURCE_DIR}/ThunderGraft.cpp
		${TG_SOURCE_DIR}/ThunderGraftPlugin.cpp
		${TG_SOURCE_DIR}/MiniaudioBackend.cpp
		# Must be an object of the plugin itself; see XpCompat.h
		${TG_SOURCE_DIR}/XpCompat.cpp
		${TG_SOURCE_DIR}/ThunderGraft.rc
		${TG_COMMON_DIR}/MPQDPluginTemplate.def
	)
	set_target_properties(ThunderGraft PROPERTIES
		PREFIX ""
		SUFFIX ".qdp"
		OUTPUT_NAME_DEBUG ThunderGraftD
	)
	target_link_libraries(ThunderGraft PRIVATE thundergraft_core psapi)
	target_compile_definitions(ThunderGraft PRIVATE ${TG_PLUGIN_DEFINITIONS})
	if(THUNDERGRAFT_VERSION)
		target_compile_definitions(ThunderGraft PRIVATE THUNDERGRAFT_VERSION="${THUNDERGRAFT_VERSION}")
	endif()
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

if(THUNDERGRAFT_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
