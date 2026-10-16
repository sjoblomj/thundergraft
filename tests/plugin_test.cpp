// Tests of the built plugin, loaded the way MPQDraft loads it, into a test program standing in for a game. The game imports the Storm functions by ordinal from a fake Storm.dll, as the real games do.

#include <doctest.h>

#include <windows.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <MPQDraftPlugin.h>
#include <QHookAPI.h>

#include "helpers/fake_storm.h"

#ifndef TG_PLUGIN_FILE
#error TG_PLUGIN_FILE must name the plugin file
#endif

namespace
{
#ifdef _DEBUG
	const DWORD EXPECTED_PLUGIN_ID = 0x3c92af17;
#else
	const DWORD EXPECTED_PLUGIN_ID = 0x3c92af16;
#endif

	// The resource IDs from ThunderGraft's resource.h
	const WORD IDD_SETTINGS = 5;
	const WORD IDB_THUNDERGRAFTLOGO = 101;

	// SFILE_DDA_LOOPING
	const DWORD DDA_LOOPING = 0x40000;

	// From PluginSettings.h
	const DWORD SETTINGS_MODULE_ID = 1;

	// The settings module the fake MPQDraft hands the plugin, if any, and the modules the plugin asked for
	std::string g_settingsModule;
	std::vector<std::pair<DWORD, DWORD>> g_requestedModules;

	struct FakeServer : public IMPQDraftServer
	{
		virtual BOOL WINAPI GetPluginModule(DWORD dwPluginID, DWORD dwModuleID, LPSTR lpszFileName)
		{
			g_requestedModules.push_back(std::make_pair(dwPluginID, dwModuleID));
			if (dwPluginID != EXPECTED_PLUGIN_ID || dwModuleID != SETTINGS_MODULE_ID || g_settingsModule.empty() || !lpszFileName)
				return FALSE;

			strcpy(lpszFileName, g_settingsModule.c_str());
			return TRUE;
		}
	};

	typedef BOOL (WINAPI *GetMPQDraftPluginPtr)(IMPQDraftPlugin **lppMPQDraftPlugin);

	// The plugin is loaded once for the whole test program, as MPQDraft only loads it once into a game
	HMODULE PluginModule()
	{
		static HMODULE hPlugin = NULL;
		if (!hPlugin)
		{
			char szPath[MAX_PATH];
			REQUIRE(GetModuleFileNameA(NULL, szPath, sizeof(szPath)));
			std::string path(szPath);
			path = path.substr(0, path.find_last_of("\\/") + 1) + TG_PLUGIN_FILE;

			hPlugin = LoadLibraryA(path.c_str());
			REQUIRE_MESSAGE(hPlugin, "Couldn't load " << path << ", error " << GetLastError());
		}

		return hPlugin;
	}

	IMPQDraftPlugin *Plugin()
	{
		static IMPQDraftPlugin *lpPlugin = NULL;
		if (!lpPlugin)
		{
			GetMPQDraftPluginPtr pfnGetMPQDraftPlugin = (GetMPQDraftPluginPtr)(void (*)(void))GetProcAddress(PluginModule(), "GetMPQDraftPlugin");
			REQUIRE(pfnGetMPQDraftPlugin);
			REQUIRE(pfnGetMPQDraftPlugin(&lpPlugin));
			REQUIRE(lpPlugin);
		}

		return lpPlugin;
	}

	/*
		Two stand-ins for other plugins hooking SFileOpenFileEx, one before ThunderGraft and one after, to check that the hooks chain. The one before does what MPQDraft's own hook does: it makes every open search all archives, even when the game names one. The one after renames files starting with alias\, so ThunderGraft sees the name it passes on, not the game's.
	*/
	typedef BOOL (WINAPI *OpenFileExPtr)(HANDLE hMpq, LPCSTR lpszFileName, DWORD dwSearchScope, HANDLE *phFile);
	OpenFileExPtr g_pfnBeforeNext = NULL, g_pfnAfterNext = NULL;
	volatile LONG g_nBeforeCalls = 0, g_nAfterCalls = 0;

	BOOL WINAPI BeforeOpenFileEx(HANDLE, LPCSTR lpszFileName, DWORD dwSearchScope, HANDLE *phFile)
	{
		InterlockedIncrement(&g_nBeforeCalls);
		return g_pfnBeforeNext(NULL, lpszFileName, dwSearchScope, phFile);
	}

	BOOL WINAPI AfterOpenFileEx(HANDLE hMpq, LPCSTR lpszFileName, DWORD dwSearchScope, HANDLE *phFile)
	{
		InterlockedIncrement(&g_nAfterCalls);
		if (lpszFileName && strncmp(lpszFileName, "alias\\", 6) == 0)
			lpszFileName += 6;
		return g_pfnAfterNext(hMpq, lpszFileName, dwSearchScope, phFile);
	}

	// Hooks SFileOpenFileEx in the game's import table, returning what it called before
	OpenFileExPtr HookOpenFileEx(OpenFileExPtr pfnHook)
	{
		ModuleFunctionMap previous;
		REQUIRE(PatchImportEntry(GetModuleHandle(NULL), GetModuleHandleA("Storm.dll"), NULL, 0x10C, (FARPROC)(void (*)(void))pfnHook, &previous, FALSE) == 1);
		REQUIRE(previous.count(GetModuleHandle(NULL)));
		return (OpenFileExPtr)(void (*)(void))previous[GetModuleHandle(NULL)];
	}

	// Files and archives the engine tests set up before the audio engine starts; see EnsureEngineStarted
	void SetUpBasePlaylist();

	void EnsureInitialized()
	{
		static bool bInitialized = false;
		if (!bInitialized)
		{
			g_pfnBeforeNext = HookOpenFileEx(BeforeOpenFileEx);

			static FakeServer server;
			REQUIRE(Plugin()->InitializePlugin(&server));

			g_pfnAfterNext = HookOpenFileEx(AfterOpenFileEx);
			bInitialized = true;
		}
	}

	HMODULE ModuleContaining(const void *lpAddress)
	{
		HMODULE hModule = NULL;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)lpAddress, &hModule);
		return hModule;
	}

	// Where the game's import table entry for a function currently points. Taking the address of a dllimport function reads it from the import table.
	template <typename Function> HMODULE ImportTarget(Function *pfnImported)
	{
		return ModuleContaining((const void *)pfnImported);
	}

	HMODULE Storm()
	{
		return GetModuleHandleA("Storm.dll");
	}
}

TEST_CASE("GetMPQDraftPlugin is exported by name and at ordinal 100")
{
	FARPROC pfnByName = GetProcAddress(PluginModule(), "GetMPQDraftPlugin");
	FARPROC pfnByOrdinal = GetProcAddress(PluginModule(), MAKEINTRESOURCEA(100));

	CHECK((const void *)pfnByName != nullptr);
	CHECK((const void *)pfnByName == (const void *)pfnByOrdinal);
	CHECK((const void *)GetProcAddress(PluginModule(), "GetMPQDraftPlugin@4") == nullptr);
}

TEST_CASE("GetMPQDraftPlugin rejects a null pointer")
{
	GetMPQDraftPluginPtr pfnGetMPQDraftPlugin = (GetMPQDraftPluginPtr)(void (*)(void))GetProcAddress(PluginModule(), "GetMPQDraftPlugin");
	REQUIRE(pfnGetMPQDraftPlugin);
	CHECK_FALSE(pfnGetMPQDraftPlugin(NULL));
}

TEST_CASE("The plugin identifies itself")
{
	DWORD dwPluginID = 0;
	REQUIRE(Plugin()->Identify(&dwPluginID));
	CHECK(dwPluginID == EXPECTED_PLUGIN_ID);
	CHECK_FALSE(Plugin()->Identify(NULL));

	char szName[MPQDRAFT_MAX_PLUGIN_NAME] = "";
	REQUIRE(Plugin()->GetPluginName(szName, sizeof(szName)));
#ifdef THUNDERGRAFT_VERSION
	std::string strExpected = "ThunderGraft v" THUNDERGRAFT_VERSION;
#else
	std::string strExpected = "ThunderGraft development build";
#endif
#ifdef _DEBUG
	strExpected += " Debug Build";
#endif
	CHECK(std::string(szName) == strExpected);

	SUBCASE("Names are truncated to the buffer")
	{
		char szShort[6];
		memset(szShort, 'x', sizeof(szShort));
		REQUIRE(Plugin()->GetPluginName(szShort, sizeof(szShort)));
		CHECK(std::string(szShort) == "Thund");
	}
}

TEST_CASE("The plugin can patch any executable and, with the default settings, needs no extra modules")
{
	CHECK(Plugin()->CanPatchExecutable("StarCraft.exe"));
	CHECK(Plugin()->ReadyForPatch());

	DWORD nModules = 1234;
	REQUIRE(Plugin()->GetModules(NULL, &nModules));
	CHECK(nModules == 0);
	CHECK_FALSE(Plugin()->GetModules(NULL, NULL));
}

TEST_CASE("The settings dialog and its logo are in the plugin's resources")
{
	CHECK(FindResourceA(PluginModule(), MAKEINTRESOURCEA(IDD_SETTINGS), (LPCSTR)RT_DIALOG));
	CHECK(FindResourceA(PluginModule(), MAKEINTRESOURCEA(IDB_THUNDERGRAFTLOGO), (LPCSTR)RT_BITMAP));
}

TEST_CASE("InitializePlugin rejects a null server")
{
	CHECK_FALSE(Plugin()->InitializePlugin(NULL));
}

TEST_CASE("InitializePlugin looks for its settings module")
{
	EnsureInitialized();

	CHECK(std::find(g_requestedModules.begin(), g_requestedModules.end(), std::make_pair(EXPECTED_PLUGIN_ID, SETTINGS_MODULE_ID)) != g_requestedModules.end());
}

TEST_CASE("InitializePlugin hooks the DDA functions in the game's import table")
{
	EnsureInitialized();

	CHECK(ImportTarget(SFileCloseFile) == PluginModule());
	CHECK(ImportTarget(SFileDdaBegin) == PluginModule());
	CHECK(ImportTarget(SFileDdaBeginEx) == PluginModule());
	CHECK(ImportTarget(SFileDdaDestroy) == PluginModule());
	CHECK(ImportTarget(SFileDdaEnd) == PluginModule());
	CHECK(ImportTarget(SFileDdaGetPos) == PluginModule());
	CHECK(ImportTarget(SFileDdaGetVolume) == PluginModule());
	CHECK(ImportTarget(SFileDdaInitialize) == PluginModule());
	CHECK(ImportTarget(SFileDdaSetVolume) == PluginModule());

	SUBCASE("And the functions that open files and archives, for playlists")
	{
		CHECK(ImportTarget(SFileOpenFile) == PluginModule());
		CHECK(ImportTarget(SFileOpenArchive) == PluginModule());
		CHECK(ImportTarget(SFileCloseArchive) == PluginModule());
		// The other plugin hooked SFileOpenFileEx after ThunderGraft did
		CHECK(ImportTarget(SFileOpenFileEx) == GetModuleHandle(NULL));
	}

	SUBCASE("The file functions it only uses are left alone")
	{
		CHECK(ImportTarget(SFileGetFileSize) == Storm());
		CHECK(ImportTarget(SFileReadFile) == Storm());
		CHECK(ImportTarget(SFileSetFilePointer) == Storm());
	}
}

TEST_CASE("Without an audio engine, the hooks forward everything to Storm")
{
	EnsureInitialized();

	HANDLE hFile = (HANDLE)0xF11E;
	IDirectSoundBuffer *lpBuffer = (IDirectSoundBuffer *)0xB0FF;
	DWORD_PTR args[7];

	LONG nCalls = FakeStormCallCount(0xFF);
	CHECK(SFileDdaBeginEx(hFile, 0x8000, DDA_LOOPING, 1234, -500, 250, lpBuffer) == FAKE_STORM_RESULT(0xFF));
	CHECK(FakeStormCallCount(0xFF) == nCalls + 1);
	FakeStormLastArgs(args);
	CHECK(args[0] == (DWORD_PTR)hFile);
	CHECK(args[1] == 0x8000);
	CHECK(args[2] == DDA_LOOPING);
	CHECK(args[3] == 1234);
	CHECK((long)args[4] == -500);
	CHECK((long)args[5] == 250);
	CHECK(args[6] == (DWORD_PTR)lpBuffer);

	DWORD nPos, nLength;
	long nVolume, nPan;

	CHECK(SFileDdaBegin(hFile, 0x8000, 0) == FAKE_STORM_RESULT(0xFE));
	CHECK(SFileDdaGetPos(hFile, &nPos, &nLength) == FAKE_STORM_RESULT(0x102));
	FakeStormLastArgs(args);
	CHECK(args[1] == (DWORD_PTR)&nPos);
	CHECK(args[2] == (DWORD_PTR)&nLength);
	CHECK(SFileDdaGetVolume(hFile, &nVolume, &nPan) == FAKE_STORM_RESULT(0x103));
	CHECK(SFileDdaSetVolume(hFile, -100, 100) == FAKE_STORM_RESULT(0x105));
	CHECK(SFileDdaEnd(hFile) == FAKE_STORM_RESULT(0x101));
	CHECK(SFileCloseFile(hFile) == FAKE_STORM_RESULT(0xFD));
	CHECK(SFileDdaInitialize((IDirectSound *)0xD5) == FAKE_STORM_RESULT(0x104));
	CHECK(SFileDdaDestroy() == FAKE_STORM_RESULT(0x100));
}

TEST_CASE("GetProcAddress hands out the hooks for Storm's DDA functions")
{
	EnsureInitialized();

	// The game's GetProcAddress import is hooked too, so games that look Storm functions up at runtime get the hooks
	for (WORD nOrdinal : { 0xFC, 0xFD, 0xFE, 0xFF, 0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x10A, 0x10B, 0x10C })
	{
		CAPTURE(nOrdinal);
		FARPROC pfnFunction = GetProcAddress(Storm(), MAKEINTRESOURCEA(nOrdinal));
		CHECK(ModuleContaining((const void *)pfnFunction) == PluginModule());
	}

	SUBCASE("Other Storm functions come from Storm")
	{
		CHECK(ModuleContaining((const void *)GetProcAddress(Storm(), MAKEINTRESOURCEA(0x109))) == Storm());
		CHECK(ModuleContaining((const void *)GetProcAddress(Storm(), MAKEINTRESOURCEA(0x108))) == Storm());
		CHECK(ModuleContaining((const void *)GetProcAddress(Storm(), "FakeStormCallCount")) == Storm());
	}

	SUBCASE("Looking up GetProcAddress itself gives the hook, so it can't be bypassed")
	{
		FARPROC pfnGetProcAddress = GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetProcAddress");
		CHECK(ModuleContaining((const void *)pfnGetProcAddress) == PluginModule());
	}

	SUBCASE("Unrelated lookups still work")
	{
		CHECK((const void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetTickCount") != nullptr);
		CHECK((const void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "NoSuchFunction") == nullptr);
	}

	SUBCASE("A hook found through GetProcAddress forwards to Storm")
	{
		typedef BOOL (WINAPI *SFileDdaEndPtr)(HANDLE hFile);
		SFileDdaEndPtr pfnEnd = (SFileDdaEndPtr)(void (*)(void))GetProcAddress(Storm(), MAKEINTRESOURCEA(0x101));

		LONG nCalls = FakeStormCallCount(0x101);
		CHECK(pfnEnd((HANDLE)0x1234) == FAKE_STORM_RESULT(0x101));
		CHECK(FakeStormCallCount(0x101) == nCalls + 1);
	}
}

TEST_CASE("The open hooks pass results and errors through unchanged")
{
	EnsureInitialized();

	typedef BOOL (WINAPI *OpenFilePtr)(LPCSTR lpszFileName, HANDLE *phFile);
	OpenFilePtr pfnStormOpenFile = (OpenFilePtr)(void (*)(void))GetExportByOrdinal(Storm(), 0x10B);
	REQUIRE(pfnStormOpenFile);

	SUBCASE("A file that doesn't exist")
	{
		HANDLE hFile = (HANDLE)0x1234;
		SetLastError(0);
		CHECK_FALSE(pfnStormOpenFile("nothere.wav", &hFile));
		DWORD dwStormError = GetLastError();
		CHECK(dwStormError != 0);

		hFile = (HANDLE)0x1234;
		SetLastError(0);
		CHECK_FALSE(SFileOpenFile("nothere.wav", &hFile));
		CHECK(GetLastError() == dwStormError);

		SetLastError(0);
		CHECK_FALSE(SFileOpenFileEx(NULL, "nothere.wav", 0, &hFile));
		CHECK(GetLastError() == dwStormError);
	}

	SUBCASE("A file that does")
	{
		HANDLE hFile = NULL;
		SetLastError(0x4321);
		REQUIRE(pfnStormOpenFile("fixtures\\wav_u8_22050_mono.wav", &hFile));
		DWORD dwStormError = GetLastError();
		SFileCloseFile(hFile);

		hFile = NULL;
		SetLastError(0x4321);
		REQUIRE(SFileOpenFile("fixtures\\wav_u8_22050_mono.wav", &hFile));
		CHECK(GetLastError() == dwStormError);
		CHECK(hFile != NULL);
		CHECK(SFileCloseFile(hFile));
	}

	SUBCASE("Archives")
	{
		const char *const lpszFiles[] = { "fixtures\\wav_u8_22050_mono.wav" };
		char szTempPath[MAX_PATH] = "";
		GetTempPathA(sizeof(szTempPath), szTempPath);
		std::string archivePath = std::string(szTempPath) + "ThunderGraft test plugin_tests hooks " + std::to_string(GetCurrentProcessId()) + ".mpq";
		REQUIRE(FakeStormCreateArchive(archivePath.c_str(), lpszFiles, 1));

		LONG nOpenCalls = FakeStormCallCount(0x10A), nCloseCalls = FakeStormCallCount(0xFC);
		HANDLE hMpq = NULL;
		REQUIRE(SFileOpenArchive(archivePath.c_str(), 0, 0, &hMpq));
		CHECK(FakeStormCallCount(0x10A) == nOpenCalls + 1);
		CHECK(SFileCloseArchive(hMpq));
		CHECK(FakeStormCallCount(0xFC) == nCloseCalls + 1);

		SetLastError(0);
		CHECK_FALSE(SFileOpenArchive("nothere.mpq", 0, 0, &hMpq));
		CHECK(GetLastError() != 0);

		DeleteFileA(archivePath.c_str());
	}
}

TEST_CASE("Other plugins' hooks on the same function still run, before and after ThunderGraft's")
{
	EnsureInitialized();

	LONG nBefore = g_nBeforeCalls, nAfter = g_nAfterCalls;
	HANDLE hFile = NULL;
	REQUIRE(SFileOpenFileEx(NULL, "alias\\fixtures\\wav_u8_22050_mono.wav", 0, &hFile));
	CHECK(g_nAfterCalls == nAfter + 1);
	CHECK(g_nBeforeCalls == nBefore + 1);
	CHECK(SFileCloseFile(hFile));
}

/*
	The tests below run in a separate process, with THUNDERGRAFT_AUDIO=null: the plugin starts its audio engine on miniaudio's null output device, and plays the sounds itself rather than forwarding to Storm.
*/

namespace
{
	const long DDA_DEFAULT = 0x7FFFFFFF;

	void EnsureEngineStarted()
	{
		static bool bStarted = false;
		if (!bStarted)
		{
			EnsureInitialized();
			SetUpBasePlaylist();

			// The game initializes DDA before using it, which is when the engine starts. Storm still gets initialized too.
			LONG nCalls = FakeStormCallCount(0x104);
			REQUIRE(SFileDdaInitialize((IDirectSound *)0xD5) == FAKE_STORM_RESULT(0x104));
			REQUIRE(FakeStormCallCount(0x104) == nCalls + 1);
			bStarted = true;
		}
	}

	// Opens a file on disk through Storm, as the game would
	HANDLE OpenFixtureFile(const char *lpszName)
	{
		std::string path = std::string("fixtures\\") + lpszName;
		HANDLE hFile = NULL;
		REQUIRE_MESSAGE(SFileOpenFileEx(NULL, path.c_str(), 0, &hFile), "Couldn't open " << path << ", error " << GetLastError());
		return hFile;
	}

	bool WaitUntil(bool (*condition)(HANDLE), HANDLE hFile, DWORD nTimeoutMs = 5000)
	{
		DWORD nStart = GetTickCount();
		while (GetTickCount() - nStart < nTimeoutMs)
		{
			if (condition(hFile))
				return true;
			Sleep(5);
		}
		return condition(hFile);
	}

	bool HasFinished(HANDLE hFile)
	{
		return !SFileDdaGetPos(hFile, NULL, NULL);
	}

	bool HasAdvanced(HANDLE hFile)
	{
		DWORD nPos = 0;
		return SFileDdaGetPos(hFile, &nPos, NULL) && nPos > 0;
	}
}

TEST_CASE("Engine: sounds play through the engine instead of Storm")
{
	EnsureEngineStarted();

	HANDLE hFile = OpenFixtureFile("ogg_vorbis_44100_stereo.ogg");
	LONG nBeginCalls = FakeStormCallCount(0xFF), nEndCalls = FakeStormCallCount(0x101);

	REQUIRE(SFileDdaBeginEx(hFile, 0x10000, 0, 0, DDA_DEFAULT, DDA_DEFAULT, NULL) == TRUE);
	CHECK(FakeStormCallCount(0xFF) == nBeginCalls);

	SUBCASE("Position and length are in bytes of 16-bit PCM")
	{
		DWORD nPos = 1, nLength = 0;
		REQUIRE(SFileDdaGetPos(hFile, &nPos, &nLength));
		CHECK(nLength == 22050 * 4);
		CHECK(nPos < nLength);

		CHECK(WaitUntil(HasAdvanced, hFile));
	}

	SUBCASE("Volume can be read and changed")
	{
		long nVolume = 1, nPan = 1;
		REQUIRE(SFileDdaGetVolume(hFile, &nVolume, &nPan));
		CHECK(nVolume == 0);
		CHECK(nPan == 0);

		CHECK(SFileDdaSetVolume(hFile, -1000, 500));
		REQUIRE(SFileDdaGetVolume(hFile, &nVolume, &nPan));
		CHECK(nVolume == -1000);
		CHECK(nPan == 500);
	}

	SUBCASE("The sound ends on its own")
	{
		CHECK(WaitUntil(HasFinished, hFile));
	}

	CHECK(SFileDdaEnd(hFile) == TRUE);
	CHECK(FakeStormCallCount(0x101) == nEndCalls);
	CHECK(SFileDdaEnd(hFile) == FALSE);

	LONG nCloseCalls = FakeStormCallCount(0xFD);
	CHECK(SFileCloseFile(hFile));
	CHECK(FakeStormCallCount(0xFD) == nCloseCalls + 1);
}

TEST_CASE("Engine: every supported format plays")
{
	EnsureEngineStarted();

	for (const char *lpszName : { "wav_s16_44100_stereo.wav", "wav_u8_22050_mono.wav", "mp3_cbr_44100_stereo.mp3", "mp3_id3v2_120k.mp3", "ogg_vorbis_48000_mono.ogg", "flac_24_96000_stereo.flac" })
	{
		std::string name = lpszName;
		CAPTURE(name);

		HANDLE hFile = OpenFixtureFile(lpszName);
		REQUIRE(SFileDdaBegin(hFile, 0x10000, 0) == TRUE);
		CHECK(WaitUntil(HasAdvanced, hFile));
		CHECK(SFileDdaEnd(hFile));
		CHECK(SFileCloseFile(hFile));
	}
}

TEST_CASE("Engine: looping sounds keep playing past their end")
{
	EnsureEngineStarted();

	HANDLE hFile = OpenFixtureFile("wav_u8_22050_mono.wav");
	REQUIRE(SFileDdaBegin(hFile, 0x10000, DDA_LOOPING) == TRUE);

	Sleep(800);

	DWORD nPos = 0, nLength = 0;
	CHECK(SFileDdaGetPos(hFile, &nPos, &nLength));
	CHECK(nLength == 11025 * 2);
	CHECK(nPos < nLength);

	CHECK(SFileDdaEnd(hFile));
	CHECK(SFileCloseFile(hFile));
}

TEST_CASE("Engine: closing a playing file waits until the sound is ended")
{
	EnsureEngineStarted();

	HANDLE hFile = OpenFixtureFile("mp3_cbr_44100_stereo.mp3");
	REQUIRE(SFileDdaBegin(hFile, 0x10000, DDA_LOOPING) == TRUE);

	LONG nCloseCalls = FakeStormCallCount(0xFD);
	CHECK(SFileCloseFile(hFile));
	CHECK(FakeStormCallCount(0xFD) == nCloseCalls);

	// Still playing from the file
	CHECK(WaitUntil(HasAdvanced, hFile));

	CHECK(SFileDdaEnd(hFile));
	CHECK(FakeStormCallCount(0xFD) == nCloseCalls + 1);
}

TEST_CASE("Engine: SFileDdaDestroy ends every sound")
{
	EnsureEngineStarted();

	HANDLE hFiles[2] = { OpenFixtureFile("flac_16_44100_stereo.flac"), OpenFixtureFile("wav_s24_48000_stereo.wav") };
	for (HANDLE hFile : hFiles)
		REQUIRE(SFileDdaBegin(hFile, 0x10000, DDA_LOOPING) == TRUE);

	LONG nDestroyCalls = FakeStormCallCount(0x100);
	CHECK(SFileDdaDestroy() == FAKE_STORM_RESULT(0x100));
	CHECK(FakeStormCallCount(0x100) == nDestroyCalls + 1);

	for (HANDLE hFile : hFiles)
	{
		CHECK_FALSE(SFileDdaGetVolume(hFile, NULL, NULL));
		CHECK(SFileCloseFile(hFile));
	}
}

TEST_CASE("Engine: files that aren't audio fail to play")
{
	EnsureEngineStarted();

	for (const char *lpszName : { "not_audio.wav", "garbage.bin", "empty.bin" })
	{
		std::string name = lpszName;
		CAPTURE(name);

		HANDLE hFile = OpenFixtureFile(lpszName);
		CHECK(SFileDdaBegin(hFile, 0x10000, 0) == FALSE);
		CHECK(SFileCloseFile(hFile));
	}
}

TEST_CASE("Engine: sounds play from an MPQ")
{
	EnsureEngineStarted();

	const char *const lpszFixtures[] = { "fixtures\\wav_s16_44100_stereo.wav", "fixtures\\mp3_id3v2_120k.mp3", "fixtures\\ogg_vorbis_44100_stereo.ogg", "fixtures\\flac_16_44100_stereo.flac" };
	char szTempPath[MAX_PATH] = "";
	GetTempPathA(sizeof(szTempPath), szTempPath);
	std::string archivePath = std::string(szTempPath) + "ThunderGraft test plugin_tests " + std::to_string(GetCurrentProcessId()) + ".mpq";
	REQUIRE(FakeStormCreateArchive(archivePath.c_str(), lpszFixtures, 4));

	HANDLE hMpq = NULL;
	REQUIRE(SFileOpenArchive(archivePath.c_str(), 0, 0, &hMpq));

	SUBCASE("Each format plays to its end")
	{
		for (const char *lpszName : { "wav_s16_44100_stereo.wav", "mp3_id3v2_120k.mp3", "ogg_vorbis_44100_stereo.ogg", "flac_16_44100_stereo.flac" })
		{
			std::string name = lpszName;
			CAPTURE(name);

			HANDLE hFile = NULL;
			REQUIRE(SFileOpenFileEx(hMpq, lpszName, 0, &hFile));
			REQUIRE(SFileDdaBegin(hFile, 0x10000, 0) == TRUE);

			DWORD nLength = 0;
			REQUIRE(SFileDdaGetPos(hFile, NULL, &nLength));
			CHECK(nLength == 22050 * 4);
			CHECK(WaitUntil(HasAdvanced, hFile));
			CHECK(WaitUntil(HasFinished, hFile));

			CHECK(SFileDdaEnd(hFile));
			CHECK(SFileCloseFile(hFile));
		}
	}

	SUBCASE("Closing a playing file from the archive waits until the sound is ended")
	{
		HANDLE hFile = NULL;
		REQUIRE(SFileOpenFileEx(hMpq, "ogg_vorbis_44100_stereo.ogg", 0, &hFile));
		REQUIRE(SFileDdaBegin(hFile, 0x10000, DDA_LOOPING) == TRUE);

		LONG nCloseCalls = FakeStormCallCount(0xFD);
		CHECK(SFileCloseFile(hFile));
		CHECK(FakeStormCallCount(0xFD) == nCloseCalls);
		CHECK(WaitUntil(HasAdvanced, hFile));

		CHECK(SFileDdaEnd(hFile));
		CHECK(FakeStormCallCount(0xFD) == nCloseCalls + 1);
	}

	CHECK(SFileCloseArchive(hMpq));
	DeleteFileA(archivePath.c_str());
}

/*
	Playlists, with the engine on the null device. The base playlist file is in an archive the "game" opens before it initializes DDA, as MPQDraft's are; see SetUpBasePlaylist. Tracks are told apart by their lengths, which GetPos reports:
	- music\title.wav, the game's own file: wav_u8_22050_mono.wav, 22050 bytes
	- music\tg_title.flac, which replaces it: flac_24_96000_stereo.flac, 192000 bytes
	- music\tg_boss.ogg, which a cue plays: ogg_vorbis_48000_mono.ogg, 48000 bytes
	- music\map.ogg, which a map's playlist file plays: ogg_vorbis_44100_stereo.ogg, 88200 bytes
*/

namespace
{
	const DWORD GAME_TITLE_LENGTH = 22050;
	const DWORD TITLE_TRACK_LENGTH = 192000;
	const DWORD BOSS_TRACK_LENGTH = 48000;
	const DWORD MAP_TRACK_LENGTH = 88200;

	const char *const BASE_PLAYLIST = R"(version = 1
[on]
'music\title.wav' = "title"
'sound\cue.wav' = { music = "boss" }

[playlist.title]
tracks = ['music\tg_title.flac']

[playlist.boss]
tracks = ['music\tg_boss.ogg']
next = "back"
)";

	std::string g_baseArchivePath;

	std::string TempPath(const std::string &name)
	{
		char szTempPath[MAX_PATH] = "";
		GetTempPathA(sizeof(szTempPath), szTempPath);
		return std::string(szTempPath) + "ThunderGraft test plugin " + std::to_string(GetCurrentProcessId()) + " " + name;
	}

	void WriteTextFile(const std::string &path, const std::string &text)
	{
		HANDLE hFile = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		REQUIRE(hFile != INVALID_HANDLE_VALUE);
		DWORD nWritten = 0;
		WriteFile(hFile, text.data(), (DWORD)text.size(), &nWritten, NULL);
		CloseHandle(hFile);
		REQUIRE(nWritten == text.size());
	}

	// Makes an archive of files on disk, each stored under the name given with it
	void CreateArchive(const std::string &path, const std::vector<std::pair<std::string, std::string>> &files)
	{
		std::vector<LPCSTR> local, archived;
		for (const std::pair<std::string, std::string> &file : files)
		{
			local.push_back(file.first.c_str());
			archived.push_back(file.second.c_str());
		}
		REQUIRE(FakeStormCreateArchiveEx(path.c_str(), local.data(), archived.data(), (DWORD)files.size()));
	}

	// The directory of the test program, which stands in for the game
	std::string GameDirectory()
	{
		char szPath[MAX_PATH];
		REQUIRE(GetModuleFileNameA(NULL, szPath, sizeof(szPath)));
		std::string path(szPath);
		return path.substr(0, path.find_last_of("\\/") + 1);
	}

	std::string LogPath()
	{
		return GameDirectory() + "ThunderGraft.log";
	}

	// Also while the plugin is writing to it
	std::string ReadTextFile(const std::string &path)
	{
		HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile == INVALID_HANDLE_VALUE)
			return "";

		std::string text(GetFileSize(hFile, NULL), '\0');
		DWORD nRead = 0;
		if (!text.empty())
			ReadFile(hFile, &text[0], (DWORD)text.size(), &nRead, NULL);
		CloseHandle(hFile);
		text.resize(nRead);
		return text;
	}

	std::string ReadLog()
	{
		return ReadTextFile(LogPath());
	}

	void SetUpBasePlaylist()
	{
		// The log is written fresh each time the game starts, and only when there is something to say
		DeleteFileA(LogPath().c_str());

		bool bBroken = GetEnvironmentVariableA("TG_TEST_BROKEN_PLAYLIST", NULL, 0) > 0;
		std::string tomlPath = TempPath("playlist.toml");
		WriteTextFile(tomlPath, bBroken ? "version = 1\nnonsense = 1\n" : BASE_PLAYLIST);

		std::string archivePath = TempPath("base.mpq");
		CreateArchive(archivePath, {
			{ tomlPath, "music\\playlist.toml" },
			{ "fixtures\\wav_u8_22050_mono.wav", "music\\title.wav" },
			{ "fixtures\\wav_s16_44100_stereo.wav", "sound\\cue.wav" },
			{ "fixtures\\flac_24_96000_stereo.flac", "music\\tg_title.flac" },
			{ "fixtures\\ogg_vorbis_48000_mono.ogg", "music\\tg_boss.ogg" },
		});
		DeleteFileA(tomlPath.c_str());

		g_baseArchivePath = archivePath;
		if (GetEnvironmentVariableA("TG_TEST_LATE_PLAYLIST", NULL, 0) > 0)
			return;

		// Opened before DDA is initialized, so it's part of the base, and left open for the rest of the run, as a game's archives are. MPQDraft gives its archives a priority this high.
		HANDLE hMpq = NULL;
		REQUIRE(SFileOpenArchive(archivePath.c_str(), 10000, 0, &hMpq));
	}

	HANDLE OpenGameFile(const char *lpszName)
	{
		HANDLE hFile = NULL;
		REQUIRE_MESSAGE(SFileOpenFileEx(NULL, lpszName, 0, &hFile), "Couldn't open " << lpszName << ", error " << GetLastError());
		return hFile;
	}

	DWORD Length(HANDLE hFile)
	{
		DWORD nLength = 0;
		SFileDdaGetPos(hFile, NULL, &nLength);
		return nLength;
	}

	DWORD Position(HANDLE hFile)
	{
		DWORD nPos = 0;
		SFileDdaGetPos(hFile, &nPos, NULL);
		return nPos;
	}

	bool WaitForLength(HANDLE hFile, DWORD nLength, DWORD nTimeoutMs = 5000)
	{
		DWORD nStart = GetTickCount();
		while (GetTickCount() - nStart < nTimeoutMs)
		{
			if (Length(hFile) == nLength)
				return true;
			Sleep(5);
		}
		return Length(hFile) == nLength;
	}

	void EndAndClose(HANDLE hFile)
	{
		SFileDdaEnd(hFile);
		SFileCloseFile(hFile);
	}
}

TEST_CASE("Engine: a playlist plays in place of the game's music")
{
	EnsureEngineStarted();

	SUBCASE("Opened with SFileOpenFileEx")
	{
		HANDLE hFile = OpenGameFile("music\\title.wav");
		REQUIRE(SFileDdaBegin(hFile, 0x10000, 0) == TRUE);
		CHECK(Length(hFile) == TITLE_TRACK_LENGTH);
		CHECK(WaitUntil(HasAdvanced, hFile));
		EndAndClose(hFile);
	}

	SUBCASE("Opened with SFileOpenFile")
	{
		HANDLE hFile = NULL;
		REQUIRE(SFileOpenFile("MUSIC\\Title.WAV", &hFile));
		REQUIRE(SFileDdaBegin(hFile, 0x10000, 0) == TRUE);
		CHECK(Length(hFile) == TITLE_TRACK_LENGTH);
		EndAndClose(hFile);
	}

	SUBCASE("Under the name the plugin hooked after ThunderGraft passes on")
	{
		HANDLE hFile = OpenGameFile("alias\\music\\title.wav");
		REQUIRE(SFileDdaBeginEx(hFile, 0x10000, 0, 0, DDA_DEFAULT, DDA_DEFAULT, NULL) == TRUE);
		CHECK(Length(hFile) == TITLE_TRACK_LENGTH);
		EndAndClose(hFile);
	}

	SUBCASE("When the playlist's visit ends, the game sees the music end")
	{
		HANDLE hFile = OpenGameFile("music\\title.wav");
		REQUIRE(SFileDdaBegin(hFile, 0x10000, 0) == TRUE);
		CHECK(WaitUntil(HasFinished, hFile));
		EndAndClose(hFile);
	}

	SUBCASE("Other files play as the game asked")
	{
		HANDLE hFile = OpenFixtureFile("wav_u8_22050_mono.wav");
		REQUIRE(SFileDdaBegin(hFile, 0x10000, 0) == TRUE);
		CHECK(Length(hFile) == GAME_TITLE_LENGTH);
		EndAndClose(hFile);
	}
}

TEST_CASE("Engine: a cue changes the music, and back returns to it")
{
	EnsureEngineStarted();

	HANDLE hMusic = OpenGameFile("music\\title.wav");
	REQUIRE(SFileDdaBegin(hMusic, 0x10000, 0) == TRUE);
	REQUIRE(Length(hMusic) == TITLE_TRACK_LENGTH);
	REQUIRE(WaitUntil(HasAdvanced, hMusic));

	HANDLE hCue = OpenGameFile("sound\\cue.wav");
	REQUIRE(SFileDdaBegin(hCue, 0x10000, 0) == TRUE);
	DWORD nInterruptedAt = Position(hMusic);

	// The cue plays as the game asked, and the music switches to the boss track
	CHECK(Length(hCue) == 88200);
	CHECK(Length(hMusic) == BOSS_TRACK_LENGTH);

	// Once the boss track has played, the title track comes back, from about where it was
	CHECK(WaitForLength(hMusic, TITLE_TRACK_LENGTH));
	CHECK(Position(hMusic) >= nInterruptedAt);

	EndAndClose(hCue);
	EndAndClose(hMusic);
}

TEST_CASE("Engine: a map's playlist file is used while its archive is open")
{
	EnsureEngineStarted();

	std::string tomlPath = TempPath("map playlist.toml");
	WriteTextFile(tomlPath, "version = 1\n[on]\n'music\\title.wav' = 'maptitle'\n[playlist.maptitle]\ntracks = ['music\\map.ogg']\n");
	std::string mapPath = TempPath("map.scm");
	CreateArchive(mapPath, {
		{ tomlPath, "music\\playlist.toml" },
		{ "fixtures\\ogg_vorbis_44100_stereo.ogg", "music\\map.ogg" },
	});
	DeleteFileA(tomlPath.c_str());

	// The base archive has a music\playlist.toml too, and a higher priority. The other plugin makes every open search all archives, as MPQDraft does, so ThunderGraft has to read the map's file from the map's archive with Storm's own function.
	HANDLE hMap = NULL;
	REQUIRE(SFileOpenArchive(mapPath.c_str(), 0, 0, &hMap));

	HANDLE hMusic = OpenGameFile("music\\title.wav");
	REQUIRE(SFileDdaBegin(hMusic, 0x10000, DDA_LOOPING) == TRUE);
	CHECK(Length(hMusic) == MAP_TRACK_LENGTH);

	SUBCASE("When the map is unloaded, its track stops and the music goes by the base file again")
	{
		LONG nCloseCalls = FakeStormCallCount(0xFD);
		CHECK(SFileCloseArchive(hMap));
		CHECK(FakeStormCallCount(0xFD) > nCloseCalls);
		CHECK(Length(hMusic) == TITLE_TRACK_LENGTH);
		hMap = NULL;
	}

	SUBCASE("Other streams of the map's files keep playing as long as the game plays them")
	{
		HANDLE hMapFile = NULL;
		REQUIRE(SFileOpenFileEx(hMap, "music\\map.ogg", 0, &hMapFile));
		REQUIRE(SFileDdaBegin(hMapFile, 0x10000, 0) == TRUE);
		EndAndClose(hMapFile);
	}

	EndAndClose(hMusic);
	if (hMap)
		CHECK(SFileCloseArchive(hMap));
	DeleteFileA(mapPath.c_str());

	CHECK(ReadLog().find("map loaded: music\\playlist.toml in " + mapPath) != std::string::npos);
}

TEST_CASE("Engine: every transition is written to ThunderGraft.log when asked to")
{
	EnsureEngineStarted();

	HANDLE hFile = OpenGameFile("music\\title.wav");
	REQUIRE(SFileDdaBegin(hFile, 0x10000, 0) == TRUE);
	EndAndClose(hFile);

	std::string log = ReadLog();
	INFO(log);
	CHECK(log.compare(0, 12, "ThunderGraft") == 0);
	CHECK(log.find("start 'music\\title.wav' in stream ") != std::string::npos);
	CHECK(log.find("[on] 'music\\title.wav' (music\\playlist.toml:3) -> \"title\"") != std::string::npos);
	CHECK(log.find("error") == std::string::npos);
}

/*
	A separate process, with TG_TEST_BROKEN_PLAYLIST set: the base playlist file has an error.
*/

TEST_CASE("Broken playlist: the game's music plays, and ThunderGraft.log says why")
{
	EnsureEngineStarted();

	HANDLE hFile = OpenGameFile("music\\title.wav");
	REQUIRE(SFileDdaBegin(hFile, 0x10000, 0) == TRUE);
	CHECK(Length(hFile) == GAME_TITLE_LENGTH);
	EndAndClose(hFile);

	std::string log = ReadLog();
	INFO(log);
	CHECK(log.find("music\\playlist.toml:2: error: unknown key \"nonsense\"") != std::string::npos);
	CHECK(log.find("The playlist file has errors, so it isn't used") != std::string::npos);
}

/*
	A separate process, with TG_TEST_LATE_PLAYLIST set: the archive with the base playlist file is only opened once DDA is initialized, and not through the game's imports, as MPQDraft opens its archives when the game opens its first.
*/

TEST_CASE("Late playlist: a base playlist file in an archive opened after DDA starts is found when the game first plays something")
{
	EnsureEngineStarted();

	typedef BOOL (WINAPI *OpenArchivePtr)(LPCSTR lpszArchiveName, DWORD dwPriority, DWORD dwFlags, HANDLE *phMpq);
	OpenArchivePtr pfnStormOpenArchive = (OpenArchivePtr)(void (*)(void))GetExportByOrdinal(Storm(), 0x10A);
	REQUIRE(pfnStormOpenArchive);
	HANDLE hMpq = NULL;
	REQUIRE(pfnStormOpenArchive(g_baseArchivePath.c_str(), 10000, 0, &hMpq));

	HANDLE hFile = OpenGameFile("music\\title.wav");
	REQUIRE(SFileDdaBegin(hFile, 0x10000, 0) == TRUE);
	CHECK(Length(hFile) == TITLE_TRACK_LENGTH);
	EndAndClose(hFile);

	// It was found as the base file, not as a map's
	CHECK(ReadLog().find("map loaded") == std::string::npos);
}

/*
	A separate process, with settings remembered in the registry as the Configure dialog leaves them: they go to the game as a module, which turns the debug log on.
*/

namespace
{
	const char SETTINGS_KEY[] = "Software\\ThunderGraft";

	// Keeps the remembered settings there were, and puts them back when it goes
	struct RememberedSettings
	{
		bool bExisted;
		std::vector<std::pair<std::string, std::vector<BYTE>>> values;
		std::vector<DWORD> types;

		RememberedSettings() : bExisted(false)
		{
			HKEY hKey;
			if (RegOpenKeyExA(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
				return;
			bExisted = true;
			for (const char *lpszName : { "DebugLog", "DebugLogFile" })
			{
				BYTE data[MAX_PATH];
				DWORD dwType = 0, nSize = sizeof(data);
				if (RegQueryValueExA(hKey, lpszName, NULL, &dwType, data, &nSize) == ERROR_SUCCESS)
				{
					values.push_back(std::make_pair(std::string(lpszName), std::vector<BYTE>(data, data + nSize)));
					types.push_back(dwType);
				}
			}
			RegCloseKey(hKey);
		}

		~RememberedSettings()
		{
			RegDeleteKeyA(HKEY_CURRENT_USER, SETTINGS_KEY);
			HKEY hKey;
			if (!bExisted || RegCreateKeyExA(HKEY_CURRENT_USER, SETTINGS_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
				return;
			for (size_t i = 0; i < values.size(); i++)
				RegSetValueExA(hKey, values[i].first.c_str(), 0, types[i], values[i].second.data(), (DWORD)values[i].second.size());
			RegCloseKey(hKey);
		}
	};
}

TEST_CASE("Settings: remembered settings go to the game as a module, and turn the debug log on")
{
	const char szLogName[] = "ThunderGraft test debug.log";
	std::string logPath = GameDirectory() + szLogName;
	DeleteFileA(logPath.c_str());

	// Before the plugin first reads them. Whatever was there is put back afterwards, in case the tests run outside a Wine prefix of their own.
	RememberedSettings remembered;
	HKEY hKey;
	REQUIRE(RegCreateKeyExA(HKEY_CURRENT_USER, SETTINGS_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS);
	DWORD dwEnabled = 1;
	CHECK(RegSetValueExA(hKey, "DebugLog", 0, REG_DWORD, (const BYTE *)&dwEnabled, sizeof(dwEnabled)) == ERROR_SUCCESS);
	CHECK(RegSetValueExA(hKey, "DebugLogFile", 0, REG_SZ, (const BYTE *)szLogName, sizeof(szLogName)) == ERROR_SUCCESS);
	RegCloseKey(hKey);

	DWORD nModules = 0;
	REQUIRE(Plugin()->GetModules(NULL, &nModules));
	REQUIRE(nModules == 1);

	MPQDRAFTPLUGINMODULE module;
	memset(&module, 0xCC, sizeof(module));
	REQUIRE(Plugin()->GetModules(&module, &nModules));
	CHECK(nModules == 1);
	CHECK(module.dwComponentID == EXPECTED_PLUGIN_ID);
	CHECK(module.dwModuleID == SETTINGS_MODULE_ID);
	CHECK_FALSE(module.bExecute);

	std::string settings = ReadTextFile(module.szModuleFileName);
	INFO(settings);
	CHECK(settings.find("DebugLog=1") != std::string::npos);
	CHECK(settings.find(std::string("DebugLogFile=") + szLogName) != std::string::npos);

	// In the game, the file name alone puts the log in the game's directory
	g_settingsModule = module.szModuleFileName;
	EnsureInitialized();

	std::string log = ReadTextFile(logPath);
	INFO(log);
	CHECK(log.find("started") != std::string::npos);
	CHECK(log.find("InitializePlugin(") != std::string::npos);
	CHECK(log.find("PatchFunctionsInModule") != std::string::npos);

	DeleteFileA(module.szModuleFileName);
}
