// Tests of the built plugin, loaded the way MPQDraft loads it, into a test program standing in for a game. The game imports the Storm functions by ordinal from a fake Storm.dll, as the real games do.

#include <doctest.h>

#include <windows.h>
#include <string.h>
#include <string>

#include <MPQDraftPlugin.h>

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
	const WORD IDD_ABOUT = 5;
	const WORD IDB_THUNDERGRAFTLOGO = 101;

	// SFILE_DDA_LOOPING
	const DWORD DDA_LOOPING = 0x40000;

	struct FakeServer : public IMPQDraftServer
	{
		virtual BOOL WINAPI GetPluginModule(DWORD, DWORD, LPSTR)
		{ return FALSE; }
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

	void EnsureInitialized()
	{
		static bool bInitialized = false;
		if (!bInitialized)
		{
			static FakeServer server;
			REQUIRE(Plugin()->InitializePlugin(&server));
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

TEST_CASE("The plugin can patch any executable and needs no extra modules")
{
	CHECK(Plugin()->CanPatchExecutable("StarCraft.exe"));
	CHECK(Plugin()->ReadyForPatch());

	DWORD nModules = 1234;
	REQUIRE(Plugin()->GetModules(NULL, &nModules));
	CHECK(nModules == 0);
	CHECK_FALSE(Plugin()->GetModules(NULL, NULL));
}

TEST_CASE("The About dialog and its logo are in the plugin's resources")
{
	CHECK(FindResourceA(PluginModule(), MAKEINTRESOURCEA(IDD_ABOUT), (LPCSTR)RT_DIALOG));
	CHECK(FindResourceA(PluginModule(), MAKEINTRESOURCEA(IDB_THUNDERGRAFTLOGO), (LPCSTR)RT_BITMAP));
}

TEST_CASE("InitializePlugin rejects a null server")
{
	CHECK_FALSE(Plugin()->InitializePlugin(NULL));
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
	for (WORD nOrdinal = 0xFD; nOrdinal <= 0x105; nOrdinal++)
	{
		CAPTURE(nOrdinal);
		FARPROC pfnFunction = GetProcAddress(Storm(), MAKEINTRESOURCEA(nOrdinal));
		CHECK(ModuleContaining((const void *)pfnFunction) == PluginModule());
	}

	SUBCASE("Other Storm functions come from Storm")
	{
		CHECK(ModuleContaining((const void *)GetProcAddress(Storm(), MAKEINTRESOURCEA(0x109))) == Storm());
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

			// The game initializes DDA before using it, which is when the engine starts. Storm still gets initialized too.
			LONG nCalls = FakeStormCallCount(0x104);
			REQUIRE(SFileDdaInitialize((IDirectSound *)0xD5) == FAKE_STORM_RESULT(0x104));
			REQUIRE(FakeStormCallCount(0x104) == nCalls + 1);
			bStarted = true;
		}
	}

	HANDLE OpenFixtureFile(const char *lpszName)
	{
		std::string path = std::string("fixtures\\") + lpszName;
		HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		REQUIRE_MESSAGE(hFile != INVALID_HANDLE_VALUE, "Couldn't open " << path);
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
