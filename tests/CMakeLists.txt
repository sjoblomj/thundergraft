add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE ${CMAKE_SOURCE_DIR}/third_party/doctest)

# Registers a test program. When cross-compiling, CTest runs it through Wine
# in a Wine prefix of its own inside the build directory.
function(tg_add_test name)
	add_test(NAME ${name} COMMAND ${name} ${ARGN})
	# The tests find their fixtures relative to where they run
	set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
	if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
		set_tests_properties(${name} PROPERTIES
			ENVIRONMENT "${TG_WINE_ENVIRONMENT}"
			FIXTURES_REQUIRED wine_prefix
		)
	endif()
	set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endfunction()

if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
	# Keep Wine quiet, and keep it from offering to install Mono and Gecko
	set(TG_WINE_ENVIRONMENT
		"WINEPREFIX=${CMAKE_BINARY_DIR}/wineprefix"
		"WINEDEBUG=-all"
		"WINEDLLOVERRIDES=mscoree,mshtml="
	)
	# Creating a prefix takes a while; do it once, before the tests run in parallel
	add_test(NAME wine_prefix COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} wineboot --init)
	set_tests_properties(wine_prefix PROPERTIES
		ENVIRONMENT "${TG_WINE_ENVIRONMENT}"
		FIXTURES_SETUP wine_prefix
		TIMEOUT 300
	)
endif()

# The audio files the tests play, copied next to the test programs
file(GLOB TG_FIXTURES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/*)
list(FILTER TG_FIXTURES EXCLUDE REGEX "generate\\.sh$")
add_custom_target(test_fixtures
	COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/fixtures
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TG_FIXTURES} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/fixtures
	DEPENDS ${TG_FIXTURES}
)

# Platform-independent tests. These also build natively, without the plugin.
add_executable(portable_tests
	test_main.cpp
	conversions_test.cpp
	ogg_length_test.cpp
	engine_test.cpp
	output_test.cpp
)
target_link_libraries(portable_tests PRIVATE thundergraft_portable doctest)
add_dependencies(portable_tests test_fixtures)
tg_add_test(portable_tests)

if(WIN32)
	# Test programs run without the MinGW runtime DLLs on the path
	set(TG_TEST_LINK_OPTIONS -static -static-libgcc -static-libstdc++)
	target_link_options(portable_tests PRIVATE ${TG_TEST_LINK_OPTIONS})

	# Builds a helper DLL that exports undecorated stdcall names, as Storm does,
	# and an import library that lets callers link to them. The import library
	# made by the linker loses the @n decoration that callers refer to, so it
	# is made with dlltool from the .def file instead. Link to <name>_import.
	find_program(TG_DLLTOOL NAMES i686-w64-mingw32-dlltool dlltool REQUIRED)
	function(tg_add_helper_dll name dll_name def_file)
		add_library(${name} SHARED ${ARGN} ${def_file})
		set_target_properties(${name} PROPERTIES PREFIX "" OUTPUT_NAME ${dll_name})
		target_link_options(${name} PRIVATE -static-libgcc -Wl,--kill-at)

		set(import_library ${CMAKE_CURRENT_BINARY_DIR}/lib${name}_import.a)
		add_custom_command(
			OUTPUT ${import_library}
			COMMAND ${TG_DLLTOOL} --kill-at --input-def ${def_file} --dllname ${dll_name}.dll --output-lib ${import_library}
			DEPENDS ${def_file}
		)
		add_custom_target(${name}_import_library DEPENDS ${import_library})

		add_library(${name}_import INTERFACE)
		target_link_libraries(${name}_import INTERFACE ${import_library})
		add_dependencies(${name}_import ${name}_import_library ${name})
	endfunction()

	# StormLib, for Storm file functions that read real MPQs, with its own copies of zlib and bzip2
	set(STORM_SKIP_INSTALL ON CACHE BOOL "" FORCE)
	set(STORM_USE_BUNDLED_LIBRARIES ON CACHE BOOL "" FORCE)
	set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB ON)
	set(CMAKE_DISABLE_FIND_PACKAGE_BZip2 ON)
	add_subdirectory(${TG_THIRD_PARTY_DIR}/StormLib ${CMAKE_BINARY_DIR}/third_party/StormLib EXCLUDE_FROM_ALL)
	# Third-party code; its warnings aren't ours
	target_compile_options(storm PRIVATE -w)

	add_library(mpq_storm STATIC fakes/MpqStorm.cpp)
	target_include_directories(mpq_storm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${TG_SOURCE_DIR})
	target_link_libraries(mpq_storm PUBLIC storm)
	target_compile_options(mpq_storm PRIVATE -Wall -Wextra)

	# The DDA emulation, with a fake engine, fake Storm and fake DirectSound buffers; and with the real engine, measuring its output, also with files read from MPQs
	add_executable(dda_player_tests
		test_main.cpp
		dda_player_test.cpp
		dda_output_test.cpp
		dda_mpq_test.cpp
		fakes/FakeStorm.cpp
	)
	add_dependencies(dda_player_tests test_fixtures)
	target_link_libraries(dda_player_tests PRIVATE thundergraft_core mpq_storm doctest)
	target_link_options(dda_player_tests PRIVATE ${TG_TEST_LINK_OPTIONS})
	tg_add_test(dda_player_tests)

	# Import table patching, against two small helper DLLs
	tg_add_helper_dll(hook_target hook_target ${CMAKE_CURRENT_SOURCE_DIR}/helpers/hook_target.def helpers/hook_target.c)
	tg_add_helper_dll(hook_user hook_user ${CMAKE_CURRENT_SOURCE_DIR}/helpers/hook_user.def helpers/hook_user.c)
	target_link_libraries(hook_user PRIVATE hook_target_import)

	# Also tests the XP compatibility shim, which has to be linked as an object, as in the plugin
	add_executable(hook_api_tests test_main.cpp hook_api_test.cpp xp_compat_test.cpp ${TG_SOURCE_DIR}/XpCompat.cpp)
	target_link_libraries(hook_api_tests PRIVATE thundergraft_core hook_target_import hook_user_import doctest)
	target_link_options(hook_api_tests PRIVATE ${TG_TEST_LINK_OPTIONS})
	tg_add_test(hook_api_tests)

	# The built plugin, loaded into a stand-in for a game with a fake Storm.dll, whose file functions are StormLib's
	tg_add_helper_dll(fake_storm Storm ${CMAKE_CURRENT_SOURCE_DIR}/helpers/fake_storm.def helpers/fake_storm.cpp)
	target_link_libraries(fake_storm PRIVATE mpq_storm)
	target_link_options(fake_storm PRIVATE ${TG_TEST_LINK_OPTIONS})

	add_executable(plugin_tests test_main.cpp plugin_test.cpp)
	target_link_libraries(plugin_tests PRIVATE thundergraft_options fake_storm_import doctest)
	target_link_options(plugin_tests PRIVATE ${TG_TEST_LINK_OPTIONS})
	target_compile_definitions(plugin_tests PRIVATE TG_PLUGIN_FILE="$<TARGET_FILE_NAME:ThunderGraft>")
	if(THUNDERGRAFT_VERSION)
		target_compile_definitions(plugin_tests PRIVATE THUNDERGRAFT_VERSION="${THUNDERGRAFT_VERSION}")
	endif()
	add_dependencies(plugin_tests ThunderGraft test_fixtures)

	# Without an audio engine: everything is forwarded to Storm
	tg_add_test(plugin_tests --test-case-exclude=Engine:*)
	set_property(TEST plugin_tests APPEND PROPERTY ENVIRONMENT THUNDERGRAFT_AUDIO=none)

	# With the audio engine on a null output device: the plugin plays the sounds itself
	add_test(NAME plugin_engine_tests COMMAND plugin_tests --test-case=Engine:*)
	set_tests_properties(plugin_engine_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} TIMEOUT 120)
	if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
		set_tests_properties(plugin_engine_tests PROPERTIES ENVIRONMENT "${TG_WINE_ENVIRONMENT}" FIXTURES_REQUIRED wine_prefix)
	endif()
	set_property(TEST plugin_engine_tests APPEND PROPERTY ENVIRONMENT THUNDERGRAFT_AUDIO=null)

	# What the plugin DLL exports and imports
	find_program(TG_OBJDUMP NAMES ${CMAKE_OBJDUMP} i686-w64-mingw32-objdump objdump)
	if(TG_OBJDUMP)
		add_test(NAME plugin_dll_check
			COMMAND ${CMAKE_COMMAND} -DOBJDUMP=${TG_OBJDUMP} -DPLUGIN=$<TARGET_FILE:ThunderGraft> -P ${CMAKE_CURRENT_SOURCE_DIR}/check_plugin_dll.cmake
		)
	endif()
endif()
