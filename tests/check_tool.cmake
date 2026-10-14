# Runs a command-line tool and checks its exit status and output.
# TOOL: the program; EMULATOR: what to run it with, if anything (Wine)
# ARGS: its arguments, separated by |; EXIT: the expected exit status
# EXPECT: regular expressions the output has to match, separated by |; REJECT: ones it mustn't match
# OUTPUT_FILE: a file the tool writes, whose contents are checked against EXPECT_FILE
string(REPLACE "|" ";" args "${ARGS}")
execute_process(
	COMMAND ${EMULATOR} ${TOOL} ${args}
	RESULT_VARIABLE result
	OUTPUT_VARIABLE output
	ERROR_VARIABLE output
)
# Wine prints Windows line endings
string(REPLACE "\r" "" output "${output}")
message("${output}")

if(NOT "${result}" STREQUAL "${EXIT}")
	message(FATAL_ERROR "exited with ${result}, not ${EXIT}")
endif()

if(EXPECT)
	string(REPLACE "|" ";" expectations "${EXPECT}")
	foreach(expectation IN LISTS expectations)
		if(NOT output MATCHES "${expectation}")
			message(FATAL_ERROR "the output doesn't match: ${expectation}")
		endif()
	endforeach()
endif()

if(REJECT)
	string(REPLACE "|" ";" rejections "${REJECT}")
	foreach(rejection IN LISTS rejections)
		if(output MATCHES "${rejection}")
			message(FATAL_ERROR "the output matches: ${rejection}")
		endif()
	endforeach()
endif()

if(OUTPUT_FILE)
	file(READ "${OUTPUT_FILE}" written)
	file(READ "${EXPECT_FILE}" expected)
	string(REPLACE "\r" "" written "${written}")
	if(NOT written STREQUAL expected)
		message(FATAL_ERROR "${OUTPUT_FILE} isn't the same as ${EXPECT_FILE}:\n${written}")
	endif()
endif()
