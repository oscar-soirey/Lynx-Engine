#ifndef INI_PARSER_H
#define INI_PARSER_H

#include <string>
#include <sstream>
#include <unordered_map>

typedef struct {
	std::string value_;
	std::string section_;
}ini_elem_t;

namespace lynx
{
	class LynxIni {
	public:
		LynxIni(const char* _path);

		template<typename T>
		T Get(const char* _key)
		{
			auto it = keys_.find(_key);
			if (it == keys_.end())
			{
				throw std::runtime_error(std::string("Key not found: ") + _key);
			}

			std::istringstream ss(it->second.value_);
			T value;
			ss >> value;
			if (ss.fail())
			{
				throw std::runtime_error(std::string("Failed to convert value for key: ") + _key);
			}
			return value;
		}

		template<typename T>
		std::unordered_map<std::string, T> GetEntries()
		{
			std::unordered_map<std::string, T> result;
			for (const auto& [key, str_val] : keys_)
			{
				std::istringstream ss(str_val.value_);
				T value;
				ss >> value;
				if (ss.fail())
				{
					throw std::runtime_error(std::string("Failed to convert value for key: ") + key);
				}
				result[key] = value;
			}
			return result;
		}

		template<typename T>
		void SetKeyValue(const char* _key, T _value)
		{
			std::ostringstream ss;
			ss << _value;
			keys_[_key].value_ = ss.str();
		}

		void SaveIniFile();

	private:
		static int handler(void* user, const char* section, const char* name, const char* value);
		std::unordered_map<std::string, ini_elem_t> keys_;
		std::string file_;
	};
}
#endif