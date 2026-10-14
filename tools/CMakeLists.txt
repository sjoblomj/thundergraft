# tgplaylist, which checks, converts and simulates playlist files. Built natively and for Windows.
add_executable(tgplaylist tgplaylist.cpp)
target_link_libraries(tgplaylist PRIVATE thundergraft_portable storm)
target_compile_options(tgplaylist PRIVATE -Wall -Wextra)
if(THUNDERGRAFT_VERSION)
	target_compile_definitions(tgplaylist PRIVATE THUNDERGRAFT_VERSION="${THUNDERGRAFT_VERSION}")
endif()
if(WIN32)
	# A single program to hand to modders, without the MinGW runtime DLLs
	target_link_options(tgplaylist PRIVATE -static -static-libgcc -static-libstdc++)
endif()
