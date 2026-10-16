/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

#include "PluginSettings.h"

#include <ctype.h>
#include <string.h>

#include <algorithm>

namespace
{
	const char REGISTRY_KEY[] = "Software\\ThunderGraft";
	const char SETTINGS_SECTION[] = "ThunderGraft";
	const char DEBUG_LOG_VALUE[] = "DebugLog";
	const char DEBUG_LOG_FILE_VALUE[] = "DebugLogFile";
}

PluginSettings LoadRememberedSettings()
{
	PluginSettings settings;

	HKEY hKey;
	if (RegOpenKeyExA(HKEY_CURRENT_USER, REGISTRY_KEY, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
		return settings;

	DWORD dwValue = 0, dwType = 0, nSize = sizeof(dwValue);
	if (RegQueryValueExA(hKey, DEBUG_LOG_VALUE, NULL, &dwType, (LPBYTE)&dwValue, &nSize) == ERROR_SUCCESS && dwType == REG_DWORD)
		settings.bDebugLog = dwValue != 0;

	char szFile[MAX_PATH] = "";
	nSize = sizeof(szFile) - 1;
	if (RegQueryValueExA(hKey, DEBUG_LOG_FILE_VALUE, NULL, &dwType, (LPBYTE)szFile, &nSize) == ERROR_SUCCESS && dwType == REG_SZ)
	{
		szFile[std::min((size_t)nSize, sizeof(szFile) - 1)] = '\0';
		if (IsValidLogFileName(szFile))
			settings.debugLogFile = szFile;
	}

	RegCloseKey(hKey);

	return settings;
}

bool RememberSettings(const PluginSettings &settings)
{
	HKEY hKey;
	if (RegCreateKeyExA(HKEY_CURRENT_USER, REGISTRY_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
		return false;

	DWORD dwValue = settings.bDebugLog ? 1 : 0;
	bool bSaved = RegSetValueExA(hKey, DEBUG_LOG_VALUE, 0, REG_DWORD, (const BYTE *)&dwValue, sizeof(dwValue)) == ERROR_SUCCESS
		&& RegSetValueExA(hKey, DEBUG_LOG_FILE_VALUE, 0, REG_SZ, (const BYTE *)settings.debugLogFile.c_str(), (DWORD)settings.debugLogFile.size() + 1) == ERROR_SUCCESS;

	RegCloseKey(hKey);

	return bSaved;
}

bool WriteSettingsFile(const PluginSettings &settings, LPCSTR lpszPath)
{
	// Read back with GetPrivateProfileString, which only needs kernel32 in the game
	std::string text = std::string("[") + SETTINGS_SECTION + "]\r\n"
		+ DEBUG_LOG_VALUE + "=" + (settings.bDebugLog ? "1" : "0") + "\r\n"
		+ DEBUG_LOG_FILE_VALUE + "=" + settings.debugLogFile + "\r\n";

	HANDLE hFile = CreateFileA(lpszPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	DWORD nWritten = 0;
	bool bWritten = WriteFile(hFile, text.data(), (DWORD)text.size(), &nWritten, NULL) && nWritten == text.size();
	CloseHandle(hFile);

	return bWritten;
}

PluginSettings ReadSettingsFile(LPCSTR lpszPath)
{
	PluginSettings settings;

	settings.bDebugLog = GetPrivateProfileIntA(SETTINGS_SECTION, DEBUG_LOG_VALUE, 0, lpszPath) != 0;

	char szFile[MAX_PATH] = "";
	GetPrivateProfileStringA(SETTINGS_SECTION, DEBUG_LOG_FILE_VALUE, "", szFile, sizeof(szFile), lpszPath);
	if (IsValidLogFileName(szFile))
		settings.debugLogFile = szFile;

	return settings;
}

bool IsValidLogFileName(const std::string &name)
{
	if (name.empty() || name.size() >= MAX_PATH)
		return false;

	// A directory rather than a file
	char cLast = name[name.size() - 1];
	if (cLast == '\\' || cLast == '/')
		return false;

	for (size_t i = 0; i < name.size(); i++)
	{
		unsigned char c = (unsigned char)name[i];
		if (c < 32 || strchr("*?\"<>|", c))
			return false;
		// Only after a drive letter
		if (c == ':' && (i != 1 || !isalpha((unsigned char)name[0])))
			return false;
	}

	return true;
}

std::string ResolveLogPath(const std::string &setting, const std::string &gameDirectory)
{
	// A drive letter, a UNC path, or a path from the root of the current drive
	bool bFullPath = (setting.size() >= 2 && setting[1] == ':') || (!setting.empty() && (setting[0] == '\\' || setting[0] == '/'));
	if (bFullPath)
		return setting;

	return gameDirectory + setting;
}
