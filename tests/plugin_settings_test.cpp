// Tests for the settings from the Configure dialog: where the debug log goes, which file names are accepted, and the settings module they travel to the game in. The registry side is tested on the built plugin, in plugin_test.cpp.

#include <doctest.h>

#include <windows.h>
#include <string>

#include "PluginSettings.h"

TEST_CASE("A debug log file name alone, or a relative path, goes in the game's directory")
{
	const std::string game = "C:\\Games\\StarCraft\\";

	CHECK(ResolveLogPath("tg.log", game) == "C:\\Games\\StarCraft\\tg.log");
	CHECK(ResolveLogPath("logs\\tg.log", game) == "C:\\Games\\StarCraft\\logs\\tg.log");

	SUBCASE("A full path is used as it is")
	{
		CHECK(ResolveLogPath("D:\\logs\\tg.log", game) == "D:\\logs\\tg.log");
		CHECK(ResolveLogPath("\\\\server\\share\\tg.log", game) == "\\\\server\\share\\tg.log");
		CHECK(ResolveLogPath("\\logs\\tg.log", game) == "\\logs\\tg.log");
		CHECK(ResolveLogPath("D:/logs/tg.log", game) == "D:/logs/tg.log");
	}
}

TEST_CASE("Debug log file names Windows can't use are rejected")
{
	CHECK(IsValidLogFileName("tg.log"));
	CHECK(IsValidLogFileName("C:\\Program Files (x86)\\StarCraft\\tg debug.log"));
	CHECK(IsValidLogFileName("\\\\server\\share\\tg.log"));

	CHECK_FALSE(IsValidLogFileName(""));
	CHECK_FALSE(IsValidLogFileName("logs\\"));
	CHECK_FALSE(IsValidLogFileName("tg?.log"));
	CHECK_FALSE(IsValidLogFileName("tg*.log"));
	CHECK_FALSE(IsValidLogFileName("\"tg.log\""));
	CHECK_FALSE(IsValidLogFileName("tg<1>.log"));
	CHECK_FALSE(IsValidLogFileName("tg|.log"));
	CHECK_FALSE(IsValidLogFileName("tg\t.log"));
	CHECK_FALSE(IsValidLogFileName("C:\\logs\\tg:2.log"));
	CHECK_FALSE(IsValidLogFileName("1:\\tg.log"));
	CHECK_FALSE(IsValidLogFileName(std::string(MAX_PATH, 'a')));
}

TEST_CASE("The settings module reads back as it was written")
{
	char szTempPath[MAX_PATH] = "";
	REQUIRE(GetTempPathA(sizeof(szTempPath), szTempPath));
	std::string path = std::string(szTempPath) + "ThunderGraft test settings " + std::to_string(GetCurrentProcessId()) + ".ini";

	PluginSettings settings;
	CHECK_FALSE(settings.bDebugLog);
	CHECK(settings.debugLogFile == DEFAULT_DEBUG_LOG_NAME);
	CHECK_FALSE(settings.NeedsModule());

	settings.bDebugLog = true;
	settings.debugLogFile = "C:\\My Logs\\tg = debug; log.txt";
	CHECK(settings.NeedsModule());
	REQUIRE(WriteSettingsFile(settings, path.c_str()));

	PluginSettings read = ReadSettingsFile(path.c_str());
	CHECK(read.bDebugLog);
	CHECK(read.debugLogFile == settings.debugLogFile);

	SUBCASE("Written again, the file is replaced")
	{
		settings.bDebugLog = false;
		REQUIRE(WriteSettingsFile(settings, path.c_str()));
		CHECK_FALSE(ReadSettingsFile(path.c_str()).bDebugLog);
	}

	DeleteFileA(path.c_str());

	SUBCASE("A missing or damaged file gives the defaults")
	{
		read = ReadSettingsFile(path.c_str());
		CHECK_FALSE(read.bDebugLog);
		CHECK(read.debugLogFile == DEFAULT_DEBUG_LOG_NAME);
	}
}
