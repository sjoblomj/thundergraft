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
	playlist_output_test.cpp
)
target_link_libraries(portable_tests PRIVATE thundergraft_portable doctest)
add_dependencies(portable_tests test_fixtures)
tg_add_test(portable_tests)

# The playlists: file format, SFPls conversion, the order tracks play in, and the player, against a fake engine and with the simulator
add_executable(playlist_tests
	test_main.cpp
	playlist_path_test.cpp
	playlist_file_test.cpp
	sfpls_converter_test.cpp
	playlist_visit_test.cpp
	playlist_player_test.cpp
	playlist_simulator_test.cpp
)
target_link_libraries(playlist_tests PRIVATE thundergraft_portable doctest)
target_include_directories(playlist_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
tg_add_test(playlist_tests)

if(WIN32)
	# Test programs run without the MinGW runtime DLLs on the path
	set(TG_TEST_LINK_OPTIONS -static -static-libgcc -static-libstdc++)
	target_link_options(portable_tests PRIVATE ${TG_TEST_LINK_OPTIONS})
	target_link_options(playlist_tests PRIVATE ${TG_TEST_LINK_OPTIONS})

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

# The tgplaylist tool, run on the files in playlists/
if(THUNDERGRAFT_BUILD_TOOLS)
	function(tg_add_tool_test name)
		cmake_parse_arguments(PARSE_ARGV 1 TOOL_TEST "" "EXIT;OUTPUT_FILE;EXPECT_FILE" "ARGS;EXPECT;REJECT")
		string(REPLACE ";" "|" args "${TOOL_TEST_ARGS}")
		string(REPLACE ";" "|" expect "${TOOL_TEST_EXPECT}")
		string(REPLACE ";" "|" reject "${TOOL_TEST_REJECT}")
		add_test(NAME ${name}
			COMMAND ${CMAKE_COMMAND}
				"-DTOOL=$<TARGET_FILE:tgplaylist>"
				"-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
				"-DARGS=${args}"
				"-DEXIT=${TOOL_TEST_EXIT}"
				"-DEXPECT=${expect}"
				"-DREJECT=${reject}"
				"-DOUTPUT_FILE=${TOOL_TEST_OUTPUT_FILE}"
				"-DEXPECT_FILE=${TOOL_TEST_EXPECT_FILE}"
				-P ${CMAKE_CURRENT_SOURCE_DIR}/check_tool.cmake
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/playlists
		)
		set_tests_properties(${name} PROPERTIES TIMEOUT 60)
		if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
			set_tests_properties(${name} PROPERTIES ENVIRONMENT "${TG_WINE_ENVIRONMENT}" FIXTURES_REQUIRED wine_prefix)
		endif()
	endfunction()

	tg_add_tool_test(tgplaylist_check EXIT 0 ARGS check example.toml EXPECT "No problems found")
	tg_add_tool_test(tgplaylist_check_errors EXIT 1 ARGS check broken.toml
		EXPECT
			"broken.toml:4: error: playlist \"terran\" has an unknown key \"tracsk\""
			"broken.toml:3: error: playlist \"terran\" has no tracks"
			"broken.toml:5: error: there is no playlist named \"zerg\""
			"3 errors, 0 warnings. ThunderGraft won't use a file with errors.")
	tg_add_tool_test(tgplaylist_check_lists EXIT 0 ARGS check lists.txt EXPECT "No problems found")
	tg_add_tool_test(tgplaylist_check_unconvertible EXIT 1 ARGS check unconvertible.txt EXPECT "unconvertible.txt:2: error: there is no list <nothing>")
	tg_add_tool_test(tgplaylist_check_files EXIT 0 ARGS check --on-disk --dir tracks ambiguous.toml
		EXPECT
			"ambiguous.toml:4: warning: .on.: 'music.t.\\.wav' and 'music..1\\.wav' .line 5. are equally specific"
			"track 'music.missing\\.ogg' isn't in the given directories or archives"
		REJECT "t1\\.ogg' isn't" "T2\\.OGG' isn't")
	tg_add_tool_test(tgplaylist_check_absolute EXIT 1 ARGS check ambiguous.toml EXPECT "is an absolute path")

	# An archive to look for tracks in, made before the test that uses it
	add_executable(make_mpq helpers/make_mpq.cpp)
	target_link_libraries(make_mpq PRIVATE storm)
	if(WIN32)
		target_link_options(make_mpq PRIVATE -static -static-libgcc -static-libstdc++)
	endif()
	add_test(NAME tgplaylist_make_archive COMMAND make_mpq ${CMAKE_CURRENT_BINARY_DIR}/tracks.mpq "music\\t1.ogg=tracks/Music/t1.ogg")
	set_tests_properties(tgplaylist_make_archive PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/playlists FIXTURES_SETUP tool_archive TIMEOUT 60)
	if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
		set_tests_properties(tgplaylist_make_archive PROPERTIES ENVIRONMENT "${TG_WINE_ENVIRONMENT}" FIXTURES_REQUIRED wine_prefix)
	endif()
	tg_add_tool_test(tgplaylist_check_archive EXIT 0 ARGS check --on-disk --mpq ${CMAKE_CURRENT_BINARY_DIR}/tracks.mpq ambiguous.toml
		EXPECT "track 'music.missing\\.ogg' isn't in" "track 'music.T2\\.OGG' isn't in"
		REJECT "t1\\.ogg' isn't")
	set_property(TEST tgplaylist_check_archive APPEND PROPERTY FIXTURES_REQUIRED tool_archive)
	tg_add_tool_test(tgplaylist_check_map EXIT 0 ARGS check --map map.toml
		EXPECT "the base file has to provide" "\"boss\" .line 5.")
	tg_add_tool_test(tgplaylist_check_map_with_base EXIT 0 ARGS check --map map.toml --base example.toml EXPECT "No problems found")
	tg_add_tool_test(tgplaylist_check_unreadable EXIT 1 ARGS check nothere.toml EXPECT "can't read nothere.toml")
	tg_add_tool_test(tgplaylist_check_bad_archive EXIT 1 ARGS check --mpq nothere.mpq example.toml EXPECT "can't open the archive nothere.mpq")
	tg_add_tool_test(tgplaylist_convert EXIT 0 ARGS convert lists.txt ${CMAKE_CURRENT_BINARY_DIR}/lists.toml
		OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/lists.toml EXPECT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/playlists/lists.expected.toml)
	tg_add_tool_test(tgplaylist_convert_to_output EXIT 0 ARGS convert lists.txt EXPECT "\\[playlist.terran\\]")
	tg_add_tool_test(tgplaylist_convert_errors EXIT 1 ARGS convert unconvertible.txt EXPECT "nothing was written")
	tg_add_tool_test(tgplaylist_simulate EXIT 0 ARGS simulate example.toml example_events.txt
		EXPECT
			"the music .s3. plays 'music.dark_t1\\.ogg' of \"dark_terran\""
			"report plays 'staredit.wav.report_dark\\.ogg' of \"report_dark\""
			"s3 goes back to 'music.dark_t1\\.ogg' of \"dark_terran\" from 0:30")
	tg_add_tool_test(tgplaylist_simulate_errors EXIT 1 ARGS simulate broken.toml example_events.txt EXPECT "wouldn't use it")
	tg_add_tool_test(tgplaylist_usage EXIT 2 EXPECT "no command given")
	tg_add_tool_test(tgplaylist_unknown_command EXIT 2 ARGS frobnicate EXPECT "unknown command frobnicate")
	tg_add_tool_test(tgplaylist_help EXIT 0 ARGS --help EXPECT "tgplaylist check")
endif()
