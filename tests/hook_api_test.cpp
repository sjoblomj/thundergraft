// Tests for import table patching (Common/QHookAPI)

#include <doctest.h>

#include <windows.h>
#include <string.h>

#include <QHookAPI.h>

#include "helpers/hook_target.h"

namespace
{
	int WINAPI Multiply(int a, int b)
	{
		return a * b;
	}

	int WINAPI Divide(int a, int b)
	{
		return a / b;
	}

	template <typename Function> FARPROC ToFarProc(Function pfnFunction)
	{
		return (FARPROC)(void (*)(void))pfnFunction;
	}

	// doctest can't print function pointers, so compare them as plain pointers
	const void *Address(FARPROC pfnFunction)
	{
		return (const void *)pfnFunction;
	}

	HMODULE TargetModule()
	{
		HMODULE hTarget = GetModuleHandleA("hook_target.dll");
		REQUIRE(hTarget);
		return hTarget;
	}

	FARPROC OriginalAdd()
	{
		return GetProcAddress(TargetModule(), "HookTargetAdd");
	}

	FARPROC OriginalByOrdinal()
	{
		return GetProcAddress(TargetModule(), MAKEINTRESOURCEA(7));
	}

	// Puts the original functions back in every module's import table when a test is done
	struct RestoreImports
	{
		~RestoreImports()
		{
			PatchImportEntry(GetModuleHandle(NULL), TargetModule(), "HookTargetAdd", 0, OriginalAdd(), NULL, TRUE);
			PatchImportEntry(GetModuleHandle(NULL), TargetModule(), NULL, 7, OriginalByOrdinal(), NULL, TRUE);
		}
	};
}

TEST_CASE("The helper DLLs work before patching")
{
	CHECK(HookTargetAdd(3, 4) == 7);
	CHECK(HookTargetByOrdinal(10, 4) == 6);
	CHECK(CallHookTargetAdd(3, 4) == 7);
}

TEST_CASE_FIXTURE(RestoreImports, "A function imported by name can be redirected")
{
	ModuleFunctionMap moduleMap;
	DWORD nPatched = PatchImportEntry(GetModuleHandle(NULL), TargetModule(), "HookTargetAdd", 0, ToFarProc(Multiply), &moduleMap, FALSE);

	CHECK(nPatched == 1);
	CHECK(HookTargetAdd(3, 4) == 12);

	SUBCASE("The map reports what the import pointed to before")
	{
		REQUIRE(moduleMap.size() == 1);
		CHECK(moduleMap.begin()->first == GetModuleHandle(NULL));
		CHECK(Address(moduleMap.begin()->second) == Address(OriginalAdd()));
	}

	SUBCASE("Other imports from the same module are left alone")
	{
		CHECK(HookTargetByOrdinal(10, 4) == 6);
	}

	SUBCASE("Without recursion, other modules are left alone")
	{
		CHECK(CallHookTargetAdd(3, 4) == 7);
	}
}

TEST_CASE_FIXTURE(RestoreImports, "A function imported by ordinal can be redirected")
{
	DWORD nPatched = PatchImportEntry(GetModuleHandle(NULL), TargetModule(), NULL, 7, ToFarProc(Divide), NULL, FALSE);

	CHECK(nPatched == 1);
	CHECK(HookTargetByOrdinal(12, 4) == 3);
	CHECK(HookTargetAdd(3, 4) == 7);
}

TEST_CASE_FIXTURE(RestoreImports, "Recursive patching reaches modules imported by the host")
{
	ModuleFunctionMap moduleMap;
	DWORD nPatched = PatchImportEntry(GetModuleHandle(NULL), TargetModule(), "HookTargetAdd", 0, ToFarProc(Multiply), &moduleMap, TRUE);

	CHECK(nPatched == 2);
	CHECK(HookTargetAdd(3, 4) == 12);
	CHECK(CallHookTargetAdd(3, 4) == 12);

	CHECK(moduleMap.size() == 2);
	CHECK(moduleMap.count(GetModuleHandleA("hook_user.dll")) == 1);
}

TEST_CASE_FIXTURE(RestoreImports, "Without a new function, imports are only looked up")
{
	ModuleFunctionMap moduleMap;
	DWORD nFound = PatchImportEntry(GetModuleHandle(NULL), TargetModule(), "HookTargetAdd", 0, NULL, &moduleMap, TRUE);

	CHECK(nFound == 2);
	CHECK(moduleMap.size() == 2);
	CHECK(HookTargetAdd(3, 4) == 7);
	CHECK(CallHookTargetAdd(3, 4) == 7);
}

TEST_CASE_FIXTURE(RestoreImports, "Imports that were already patched by someone else are found by name")
{
	PatchImportEntry(GetModuleHandle(NULL), TargetModule(), "HookTargetAdd", 0, ToFarProc(Multiply), NULL, FALSE);

	// A second hook in the chain finds the first one, which it should call through to
	ModuleFunctionMap moduleMap;
	DWORD nPatched = PatchImportEntry(GetModuleHandle(NULL), TargetModule(), "HookTargetAdd", 0, ToFarProc(Divide), &moduleMap, FALSE);

	CHECK(nPatched == 1);
	REQUIRE(moduleMap.size() == 1);
	CHECK(Address(moduleMap.begin()->second) == Address(ToFarProc(Multiply)));
	CHECK(HookTargetAdd(12, 4) == 3);
}

TEST_CASE_FIXTURE(RestoreImports, "Functions that aren't imported aren't found")
{
	ModuleFunctionMap moduleMap;
	CHECK(PatchImportEntry(GetModuleHandle(NULL), TargetModule(), "NotExported", 0, ToFarProc(Multiply), &moduleMap, TRUE) == 0);
	CHECK(PatchImportEntry(GetModuleHandle(NULL), TargetModule(), NULL, 99, ToFarProc(Multiply), &moduleMap, TRUE) == 0);
	CHECK(moduleMap.empty());
	CHECK(HookTargetAdd(3, 4) == 7);
}

TEST_CASE_FIXTURE(RestoreImports, "Imports can also be redirected by their current address")
{
	ModuleSet modules;
	DWORD nPatched = PatchImportEntry(GetModuleHandle(NULL), "hook_target.dll", OriginalAdd(), ToFarProc(Multiply), &modules, TRUE);

	CHECK(nPatched == 2);
	CHECK(HookTargetAdd(3, 4) == 12);
	CHECK(CallHookTargetAdd(3, 4) == 12);
	CHECK(modules.count(GetModuleHandle(NULL)) == 1);

	SUBCASE("Unknown export modules fail")
	{
		CHECK(PatchImportEntry(GetModuleHandle(NULL), "not_loaded.dll", OriginalAdd(), ToFarProc(Multiply), FALSE) == (DWORD)-1);
	}
}

namespace
{
	// A module image built by hand in memory, for feeding damaged headers to the patcher. The image is two pages, followed by an inaccessible page, so reading past the end of the image crashes instead of passing silently.
	class HandMadeImage
	{
	private:
		BYTE *m_lpBase;

	public:
		static const DWORD IMAGE_SIZE = 0x2000;
		static const DWORD NT_HEADER_OFFSET = 0x80;

		HandMadeImage()
		{
			m_lpBase = (BYTE *)VirtualAlloc(NULL, IMAGE_SIZE + 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
			REQUIRE(m_lpBase);

			DWORD dwOldProtect;
			REQUIRE(VirtualProtect(m_lpBase + IMAGE_SIZE, 0x1000, PAGE_NOACCESS, &dwOldProtect));
		}

		~HandMadeImage()
		{
			VirtualFree(m_lpBase, 0, MEM_RELEASE);
		}

		HMODULE Module() const
		{ return (HMODULE)m_lpBase; }

		BYTE *At(DWORD nRVA) const
		{ return m_lpBase + nRVA; }

		PIMAGE_NT_HEADERS NtHeaders() const
		{ return (PIMAGE_NT_HEADERS)At(NT_HEADER_OFFSET); }

		// Valid DOS and PE headers, with the import directory where requested
		void WriteHeaders(DWORD nImportRVA, DWORD nImportSize)
		{
			PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)m_lpBase;
			pDosHeader->e_magic = IMAGE_DOS_SIGNATURE;
			pDosHeader->e_lfanew = NT_HEADER_OFFSET;

			PIMAGE_NT_HEADERS pNTHeader = NtHeaders();
			pNTHeader->Signature = IMAGE_NT_SIGNATURE;
			pNTHeader->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
			pNTHeader->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER);
			pNTHeader->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR_MAGIC;
			pNTHeader->OptionalHeader.SizeOfImage = IMAGE_SIZE;
			pNTHeader->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
			pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = nImportRVA;
			pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = nImportSize;
		}

		PIMAGE_IMPORT_DESCRIPTOR Descriptor(DWORD nRVA) const
		{ return (PIMAGE_IMPORT_DESCRIPTOR)At(nRVA); }

		void WriteString(DWORD nRVA, const char *lpszString)
		{ strcpy((char *)At(nRVA), lpszString); }
	};

	HMODULE Kernel32()
	{ return GetModuleHandleA("kernel32.dll"); }

	DWORD Patch(const HandMadeImage &image)
	{
		return PatchImportEntry(image.Module(), Kernel32(), "GetTickCount", 0, ToFarProc(Multiply), NULL, FALSE);
	}
}

TEST_CASE("Damaged or foreign images are rejected rather than crashing")
{
	HandMadeImage image;

	SUBCASE("Not an image at all")
	{
		CHECK(Patch(image) == 0);
	}

	SUBCASE("PE header offset pointing outside the image")
	{
		image.WriteHeaders(0, 0);
		((PIMAGE_DOS_HEADER)image.At(0))->e_lfanew = HandMadeImage::IMAGE_SIZE + 0x10;
		CHECK(Patch(image) == 0);

		((PIMAGE_DOS_HEADER)image.At(0))->e_lfanew = -0x100;
		CHECK(Patch(image) == 0);
	}

	SUBCASE("Missing PE signature")
	{
		image.WriteHeaders(0x1000, sizeof(IMAGE_IMPORT_DESCRIPTOR) * 2);
		image.NtHeaders()->Signature = 0;
		CHECK(Patch(image) == 0);
	}

	SUBCASE("No import table")
	{
		image.WriteHeaders(0, 0);
		CHECK(Patch(image) == 0);
	}

	SUBCASE("Import table extending past the end of the image")
	{
		image.WriteHeaders(HandMadeImage::IMAGE_SIZE - 0x10, 0x100);
		CHECK(Patch(image) == 0);
	}

	SUBCASE("Import table with no terminating entry")
	{
		// Exactly one descriptor fits in the directory, right before the inaccessible page
		DWORD nDescRVA = HandMadeImage::IMAGE_SIZE - sizeof(IMAGE_IMPORT_DESCRIPTOR);
		image.WriteHeaders(nDescRVA, sizeof(IMAGE_IMPORT_DESCRIPTOR));
		image.WriteString(0x1000, "not_loaded.dll");
		image.Descriptor(nDescRVA)->Name = 0x1000;
		image.Descriptor(nDescRVA)->FirstThunk = 0x1100;

		CHECK(Patch(image) == 0);
	}

	SUBCASE("Imported module name outside the image")
	{
		image.WriteHeaders(0x1000, sizeof(IMAGE_IMPORT_DESCRIPTOR) * 2);
		image.Descriptor(0x1000)->Name = HandMadeImage::IMAGE_SIZE + 0x10;
		image.Descriptor(0x1000)->FirstThunk = 0x1800;

		CHECK(Patch(image) == (DWORD)-1);
	}

	SUBCASE("Imported module name running into the end of the image")
	{
		image.WriteHeaders(0x1000, sizeof(IMAGE_IMPORT_DESCRIPTOR) * 2);
		memset(image.At(0x1F00), 'a', 0x100);
		image.Descriptor(0x1000)->Name = 0x1F00;
		image.Descriptor(0x1000)->FirstThunk = 0x1800;

		CHECK(Patch(image) == (DWORD)-1);
	}

	SUBCASE("Thunk table running into the end of the image")
	{
		image.WriteHeaders(0x1000, sizeof(IMAGE_IMPORT_DESCRIPTOR) * 2);
		image.WriteString(0x1100, "kernel32.dll");
		image.WriteString(0x1202, "SomethingElse");

		DWORD nThunkRVA = HandMadeImage::IMAGE_SIZE - sizeof(FARPROC);
		image.Descriptor(0x1000)->Name = 0x1100;
		image.Descriptor(0x1000)->FirstThunk = nThunkRVA;
		image.Descriptor(0x1000)->OriginalFirstThunk = nThunkRVA;
		// The import by name entry, at 0x1200 (hint, then name), which is also what the "loader" left in the thunk
		*(DWORD *)image.At(nThunkRVA) = 0x1200;

		CHECK(Patch(image) == (DWORD)-1);
	}

	SUBCASE("Imported function name outside the image")
	{
		image.WriteHeaders(0x1000, sizeof(IMAGE_IMPORT_DESCRIPTOR) * 2);
		image.WriteString(0x1100, "kernel32.dll");
		image.Descriptor(0x1000)->Name = 0x1100;
		image.Descriptor(0x1000)->FirstThunk = 0x1300;
		image.Descriptor(0x1000)->OriginalFirstThunk = 0x1400;
		*(DWORD *)image.At(0x1300) = 0x12345;
		*(DWORD *)image.At(0x1400) = HandMadeImage::IMAGE_SIZE + 0x100;

		CHECK(Patch(image) == (DWORD)-1);
	}

	SUBCASE("A well-formed hand-made image is patched")
	{
		image.WriteHeaders(0x1000, sizeof(IMAGE_IMPORT_DESCRIPTOR) * 2);
		image.WriteString(0x1100, "kernel32.dll");
		image.WriteString(0x1202, "GetTickCount");
		image.Descriptor(0x1000)->Name = 0x1100;
		image.Descriptor(0x1000)->FirstThunk = 0x1300;
		image.Descriptor(0x1000)->OriginalFirstThunk = 0x1400;
		*(FARPROC *)image.At(0x1300) = GetProcAddress(Kernel32(), "GetTickCount");
		*(DWORD *)image.At(0x1400) = 0x1200;

		CHECK(Patch(image) == 1);
		CHECK(Address(*(FARPROC *)image.At(0x1300)) == Address(ToFarProc(Multiply)));
	}
}

TEST_CASE_FIXTURE(RestoreImports, "Exports are found by ordinal in the export table itself")
{
	typedef int (WINAPI *TargetFunction)(int a, int b);
	TargetFunction pfnExport = (TargetFunction)(void (*)(void))GetExportByOrdinal(TargetModule(), 7);

	CHECK(Address(GetExportByOrdinal(TargetModule(), 7)) == Address(OriginalByOrdinal()));
	REQUIRE(pfnExport);
	CHECK(pfnExport(6, 7) == 6 - 7);
	// The linker numbered HookTargetAdd just before it
	CHECK(Address(GetExportByOrdinal(TargetModule(), 6)) == Address(OriginalAdd()));

	SUBCASE("Even when GetProcAddress would hand out a hook, as MPQDraft's does")
	{
		// Patching the imports doesn't change the exports, which is the point
		PatchImportEntry(GetModuleHandle(NULL), TargetModule(), NULL, 7, ToFarProc(Multiply), NULL, TRUE);
		CHECK(HookTargetByOrdinal(6, 7) == 42);
		CHECK(((TargetFunction)(void (*)(void))GetExportByOrdinal(TargetModule(), 7))(6, 7) == -1);
	}

	SUBCASE("Ordinals the module doesn't export aren't found")
	{
		CHECK(Address(GetExportByOrdinal(TargetModule(), 0)) == nullptr);
		CHECK(Address(GetExportByOrdinal(TargetModule(), 5)) == nullptr);
		CHECK(Address(GetExportByOrdinal(TargetModule(), 8)) == nullptr);
		CHECK(Address(GetExportByOrdinal(TargetModule(), 0xFFFF)) == nullptr);
	}

	SUBCASE("A module without an export table has nothing to find")
	{
		CHECK(Address(GetExportByOrdinal(NULL, 7)) == nullptr);
		CHECK(Address(GetExportByOrdinal(GetModuleHandle(NULL), 100)) == nullptr);
	}
}

TEST_CASE("Damaged export tables are rejected rather than crashing")
{
	HandMadeImage image;
	image.WriteHeaders(0, 0);

	const DWORD EXPORT_RVA = 0x1000;
	PIMAGE_DATA_DIRECTORY lpExportDir = &image.NtHeaders()->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
	lpExportDir->VirtualAddress = EXPORT_RVA;
	lpExportDir->Size = sizeof(IMAGE_EXPORT_DIRECTORY);
	PIMAGE_EXPORT_DIRECTORY lpExports = (PIMAGE_EXPORT_DIRECTORY)image.At(EXPORT_RVA);
	lpExports->Base = 10;
	lpExports->NumberOfFunctions = 2;
	lpExports->AddressOfFunctions = 0x1100;
	((LPDWORD)image.At(0x1100))[0] = 0x1800;
	((LPDWORD)image.At(0x1100))[1] = 0x1900;

	SUBCASE("A well-formed one works")
	{
		CHECK((const void *)GetExportByOrdinal(image.Module(), 10) == image.At(0x1800));
		CHECK((const void *)GetExportByOrdinal(image.Module(), 11) == image.At(0x1900));
		CHECK((const void *)GetExportByOrdinal(image.Module(), 12) == nullptr);
	}

	SUBCASE("Function table running into the end of the image")
	{
		lpExports->AddressOfFunctions = HandMadeImage::IMAGE_SIZE - sizeof(DWORD);
		CHECK((const void *)GetExportByOrdinal(image.Module(), 11) == nullptr);
	}

	SUBCASE("A count so big the table's size overflows")
	{
		lpExports->NumberOfFunctions = 0x40000001;
		CHECK((const void *)GetExportByOrdinal(image.Module(), 11) == nullptr);
	}

	SUBCASE("A function outside the image")
	{
		((LPDWORD)image.At(0x1100))[1] = HandMadeImage::IMAGE_SIZE + 0x10;
		CHECK((const void *)GetExportByOrdinal(image.Module(), 11) == nullptr);
	}

	SUBCASE("A forwarded export, whose address is the name it is forwarded to")
	{
		lpExportDir->Size = 0x200;
		((LPDWORD)image.At(0x1100))[1] = EXPORT_RVA + 0x150;
		CHECK((const void *)GetExportByOrdinal(image.Module(), 11) == nullptr);
	}

	SUBCASE("An export directory too small to be one")
	{
		lpExportDir->Size = 4;
		CHECK((const void *)GetExportByOrdinal(image.Module(), 10) == nullptr);
	}
}
