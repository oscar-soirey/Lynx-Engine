#ifndef HGE_TYPENAME_UTIL_H
#define HGE_TYPENAME_UTIL_H

#include <string>
#include <typeinfo>

#ifdef __GNUG__
	#include <cxxabi.h>
#endif

namespace lynx
{
	//Compile type version, without polymorphism
	template <typename T>
	std::string CTypeName()
	{
#ifdef _MSC_VER
		std::string name = __FUNCSIG__;
		auto start = name.find("GetTypeName<") + 12;
		auto end   = name.find(">(void)");
		return name.substr(start, end - start);
#else
		std::string name = __PRETTY_FUNCTION__;
		auto start = name.find("T = ") + 4;
		auto end   = name.find(";");
		if (end == std::string::npos)
			end = name.find("]");
		return name.substr(start, end - start);
#endif
	}

	//Runtime version, including polymorphism
	template <typename T>
	std::string ETypeName(const T& obj)
	{
		const std::type_info& ti = typeid(obj); //type dynamique réel

#ifdef __GNUG__
		int status = 0;
		char* demangled = abi::__cxa_demangle(ti.name(), nullptr, nullptr, &status);
		std::string result = (status == 0 && demangled) ? demangled : ti.name();
		free(demangled);
		return result;

#elif defined(_MSC_VER)
		std::string name = ti.name();
		//MSVC préfixe avec "class " ou "struct "
		for (const char* prefix : { "class ", "struct " })
		{
			if (name.rfind(prefix, 0) == 0)
			{
				name = name.substr(strlen(prefix));
				break;
			}
		}
		return name;

#else
		return ti.name(); //fallback brut
#endif
	}
}

#endif