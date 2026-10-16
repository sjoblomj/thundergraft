/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	The settings made in the Configure dialog. They are made in MPQDraft but used in the game, so they travel the way MPQDraft means plugins' data files to: as a plugin module, a small INI file that MPQDraft hands the plugin in the game, and packs into SEMPQs. With the default settings there is no module at all, so patches and SEMPQs made without changing them are the same as before there were settings.

	MPQDraft also remembers them in the registry, under HKEY_CURRENT_USER\Software\ThunderGraft, for the next time it is started.
*/

#ifndef PLUGINSETTINGS_H
#define PLUGINSETTINGS_H

#include <windows.h>
#include <string>

// The module ID of the settings file. 0 is the plugin itself.
#define SETTINGS_MODULE_ID 1

#define DEFAULT_DEBUG_LOG_NAME "ThunderGraft debug.log"

struct PluginSettings
{
	// Whether to write the debug log, and where: a full path, or a path relative to the game's directory
	bool bDebugLog;
	std::string debugLogFile;

	PluginSettings() : bDebugLog(false), debugLogFile(DEFAULT_DEBUG_LOG_NAME) {}

	// Whether the settings differ from the defaults in a way that matters in the game, and so need the module
	bool NeedsModule() const
	{ return bDebugLog; }
};

// The settings remembered in the registry, or the defaults where there are none
PluginSettings LoadRememberedSettings();
bool RememberSettings(const PluginSettings &settings);

// The settings module
bool WriteSettingsFile(const PluginSettings &settings, LPCSTR lpszPath);
PluginSettings ReadSettingsFile(LPCSTR lpszPath);

// Whether a debug log file name can be used: not empty, and without characters Windows doesn't allow in paths
bool IsValidLogFileName(const std::string &name);

// Where the debug log goes: the setting itself if it is a full path, or else in gameDirectory, which ends with a backslash
std::string ResolveLogPath(const std::string &setting, const std::string &gameDirectory);

#endif // #ifndef PLUGINSETTINGS_H
