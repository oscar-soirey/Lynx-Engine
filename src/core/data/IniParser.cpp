#include "IniParser.h"

#include <inih/ini.h>
#include <fstream>

namespace lynx
{
	int LynxIni::handler(void* user, const char* section, const char* name, const char* value)
	{
		auto* iniObject = static_cast<LynxIni*>(user);
		iniObject->keys_.emplace(name, ini_elem_t(value, section));
		return 1;
	}

	LynxIni::LynxIni(const char *_path) : file_(_path)
	{
		if (file_.empty())
		{
			return;
		}
		ini_parse(_path, handler, this);
	}

	void LynxIni::SaveIniFile()
	{
		//on ouvre le fichier
		std::ofstream file(file_);
		if (!file.is_open())
		{
			throw std::runtime_error("Cannot open file for writing");
		}

		// Regrouper les clés par section
		std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sections;
		for (const auto& [key, elem] : keys_)
		{
			sections[elem.section_][key] = elem.value_;
		}

		// Écrire les sections et les clés
		for (const auto& [section, keys] : sections)
		{
			file << "[" << section << "]\n";
			for (const auto& [key, value] : keys)
			{
				file << key << "=" << value << "\n";
			}
			file << "\n";
		}
	}
}