/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

#include "SfplsConverter.h"
#include "PlaylistPath.h"

#include <map>
#include <vector>

namespace
{
	struct Line
	{
		std::string text;
		unsigned nLine;
	};

	struct List
	{
		std::string name;
		// The name in the TOML file, which differs if the original can't be used
		std::string tomlName;
		std::vector<Line> files;
		unsigned nLine;
	};

	struct Mapping
	{
		std::string file;
		std::string list;
		unsigned nLine;
	};

	std::string Trim(const std::string &text)
	{
		size_t iStart = text.find_first_not_of(" \t");
		if (iStart == std::string::npos)
			return "";

		size_t iEnd = text.find_last_not_of(" \t");
		return text.substr(iStart, iEnd - iStart + 1);
	}

	// Splits into trimmed lines, numbered from 1, without the blank ones
	std::vector<Line> SplitLines(const std::string &text)
	{
		std::vector<Line> lines;
		size_t iStart = 0;
		// A UTF-8 byte order mark, as Notepad writes
		if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
			iStart = 3;

		unsigned nLine = 1;
		while (iStart <= text.size())
		{
			size_t iEnd = text.find_first_of("\r\n", iStart);
			if (iEnd == std::string::npos)
				iEnd = text.size();

			std::string line = Trim(text.substr(iStart, iEnd - iStart));
			if (!line.empty())
				lines.push_back(Line{line, nLine});

			if (iEnd < text.size() && text[iEnd] == '\r' && iEnd + 1 < text.size() && text[iEnd + 1] == '\n')
				iEnd++;

			iStart = iEnd + 1;
			nLine++;
		}

		return lines;
	}

	bool EqualsIgnoringCase(const std::string &a, const std::string &b)
	{
		return NormalizePlaylistName(a) == NormalizePlaylistName(b);
	}

	bool IsBareKey(const std::string &name)
	{
		if (name.empty())
			return false;

		for (char c : name)
		{
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
				return false;
		}

		return true;
	}

	// A TOML basic string, in double quotes
	std::string BasicString(const std::string &text)
	{
		std::string quoted = "\"";
		for (char c : text)
		{
			if (c == '"' || c == '\\')
				quoted += '\\';
			quoted += c;
		}

		return quoted + "\"";
	}

	// Paths go in single quotes, which TOML takes literally, unless they contain one
	std::string PathString(const std::string &path)
	{
		if (path.find('\'') == std::string::npos)
			return "'" + path + "'";

		return BasicString(path);
	}

	std::string PlaylistKey(const std::string &name)
	{
		return IsBareKey(name) ? name : BasicString(name);
	}

	void WriteTracks(std::string &toml, const std::vector<Line> &files)
	{
		toml += "tracks = [\n";
		for (const Line &file : files)
			toml += "    " + PathString(file.text) + ",\n";
		toml += "]\n";
	}

	bool CheckPath(const Line &line, const std::string &what, const std::string &label, PlaylistDiagnostics &diagnostics)
	{
		if (HasPlaylistControlCharacter(line.text))
		{
			diagnostics.Error(label, line.nLine, what + " contains a control character");
			return false;
		}

		return true;
	}

	bool ConvertFileList(const std::vector<Line> &lines, const std::string &label, std::string *lpToml, PlaylistDiagnostics &diagnostics)
	{
		bool bOK = true;
		for (const Line &line : lines)
			bOK = CheckPath(line, "the file name", label, diagnostics) && bOK;

		if (lines.empty())
		{
			diagnostics.Error(label, 0, "the playlist lists no files");
			return false;
		}
		if (!bOK)
			return false;

		std::string toml =
			"# Converted from " + label + "\n"
			"version = 1\n"
			"\n"
			"[on]\n"
			"'music\\*'             = \"music\"    # Diablo, StarCraft, Warcraft II\n"
			"'data\\global\\music\\*' = \"music\"    # Diablo II\n"
			"\n"
			"[playlist.music]\n"
			"order = \"in order\"\n"
			"play = \"one\"\n";
		WriteTracks(toml, lines);

		*lpToml = toml;
		return true;
	}

	bool ConvertLists(const std::vector<Line> &lines, const std::string &label, std::string *lpToml, PlaylistDiagnostics &diagnostics)
	{
		std::vector<Mapping> mappings;
		std::vector<List> lists;
		std::map<std::string, size_t> listIndex;
		bool bOK = true;

		// The first line is <SFPlaylist1>
		for (size_t iLine = 1; iLine < lines.size(); iLine++)
		{
			const Line &line = lines[iLine];
			if (line.text.size() >= 2 && line.text[0] == '<' && line.text[line.text.size() - 1] == '>')
			{
				std::string name = Trim(line.text.substr(1, line.text.size() - 2));
				if (name.empty())
				{
					diagnostics.Error(label, line.nLine, "a list has an empty name");
					bOK = false;
				}
				else if (HasPlaylistControlCharacter(name))
				{
					diagnostics.Error(label, line.nLine, "the list name contains a control character");
					bOK = false;
				}
				else if (listIndex.count(name))
				{
					diagnostics.Error(label, line.nLine, "the list <" + name + "> is already started on line " + std::to_string(lists[listIndex[name]].nLine));
					bOK = false;
				}

				listIndex[name] = lists.size();
				lists.push_back(List{name, name, std::vector<Line>(), line.nLine});
			}
			else if (lists.empty())
			{
				size_t iEquals = line.text.find('=');
				if (iEquals == std::string::npos)
				{
					diagnostics.Error(label, line.nLine, "this line is neither a mapping, such as music\\terran1.wav = terran, nor a file of a list, which have to come after a <list> line");
					bOK = false;
					continue;
				}

				Mapping mapping{Trim(line.text.substr(0, iEquals)), Trim(line.text.substr(iEquals + 1)), line.nLine};
				if (mapping.file.empty() || mapping.list.empty())
				{
					diagnostics.Error(label, line.nLine, "a mapping needs a file and a list name, as in music\\terran1.wav = terran");
					bOK = false;
					continue;
				}

				bOK = CheckPath(Line{mapping.file, line.nLine}, "the file name", label, diagnostics) && bOK;
				mappings.push_back(mapping);
			}
			else
			{
				bOK = CheckPath(line, "the file name", label, diagnostics) && bOK;
				lists.back().files.push_back(line);
			}
		}

		std::map<std::string, unsigned> mappedFiles;
		for (const Mapping &mapping : mappings)
		{
			std::string normalized = NormalizePlaylistName(mapping.file);
			if (mappedFiles.count(normalized))
			{
				diagnostics.Error(label, mapping.nLine, "the file " + mapping.file + " is already mapped on line " + std::to_string(mappedFiles[normalized]));
				bOK = false;
			}
			mappedFiles[normalized] = mapping.nLine;

			if (!listIndex.count(mapping.list))
			{
				diagnostics.Error(label, mapping.nLine, "there is no list <" + mapping.list + ">");
				bOK = false;
			}
		}

		for (List &list : lists)
		{
			if (list.files.empty())
			{
				diagnostics.Error(label, list.nLine, "the list <" + list.name + "> has no files");
				bOK = false;
			}

			// back and stop are targets of their own in ThunderGraft's format
			if (list.name == "back" || list.name == "stop")
			{
				std::string newName = list.name + "_list";
				while (listIndex.count(newName))
					newName += "_";

				list.tomlName = newName;
				diagnostics.Warning(label, list.nLine, "the list <" + list.name + "> is called \"" + newName + "\" in the converted file, as \"" + list.name + "\" has a meaning of its own there");
			}
		}

		if (!bOK)
			return false;

		std::string toml =
			"# Converted from " + label + "\n"
			"version = 1\n";

		if (!mappings.empty())
		{
			toml += "\n[on]\n";
			for (const Mapping &mapping : mappings)
				toml += PathString(PlaylistPathForStorm(mapping.file)) + " = " + BasicString(lists[listIndex[mapping.list]].tomlName) + "\n";
		}

		for (const List &list : lists)
		{
			toml += "\n[playlist." + PlaylistKey(list.tomlName) + "]\n"
				"order = \"in order\"\n"
				"play = \"one\"\n";
			WriteTracks(toml, list.files);
		}

		*lpToml = toml;
		return true;
	}
}

bool IsSfplsListFormat(const std::string &text)
{
	std::vector<Line> lines = SplitLines(text);
	return !lines.empty() && EqualsIgnoringCase(lines[0].text, "<SFPlaylist1>");
}

bool ConvertSfplsPlaylist(const std::string &text, const std::string &label, std::string *lpToml, PlaylistDiagnostics &diagnostics)
{
	try
	{
		std::vector<Line> lines = SplitLines(text);
		if (!lines.empty() && EqualsIgnoringCase(lines[0].text, "<SFPlaylist1>"))
			return ConvertLists(lines, label, lpToml, diagnostics);

		return ConvertFileList(lines, label, lpToml, diagnostics);
	}
	catch (...)
	{
		diagnostics.Error(label, 0, "not enough memory to convert the file");
		return false;
	}
}
