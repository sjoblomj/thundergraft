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
	CHECK(strncmp(szName, "ThunderGraft", 12) == 0);

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
