#include "SystemModule.h"

#include "../Engine.h"

#include <iostream>
#include <filesystem>

namespace lynx
{
#ifdef _WIN32
	SysModule::SysModule(const char *shared_file_path)
	{
		//charger la dll
		std::filesystem::path fs_path = shared_file_path;
		std::string dll_path = fs_path.string();

		sysmodule_ = LoadLibraryA(dll_path.c_str());
		if (!sysmodule_)
		{
			std::cout << "DLL module loading error : unable to load dll: " << dll_path << std::endl;
			return;
		}

		register_classes_function = (RegisterFactoryFn)GetProcAddress(sysmodule_, "FactoryRegisterClasses");
		if (!register_classes_function)
		{
			std::cout << "RegisterFactory function not found" << std::endl;
			return;
		}

		unregister_classes_function = (UnregisterFactoryFn)GetProcAddress(sysmodule_, "FactoryUnregisterClasses");
		if (!unregister_classes_function)
		{
			std::cout << "UnregisterFactory function not found" << std::endl;
			return;
		}
	}

#elif defined (__linux__)
	SysPlugin::SysPlugin(const char *parent_path)=0;
#elif defined (__APPLE__)
	SysPlugin::SysPlugin(const char *parent_path)=0;
#endif

#ifdef _WIN32
	SysModule::~SysModule()
	{
		Unload();
	}
#elif defined (__linux__)
	SysPlugin::~SysPlugin()=0;
#elif defined (__APPLE__)
	SysPlugin::~SysPlugin()=0;
#endif

	void SysModule::RegisterFactory()
	{
		if (!register_classes_function)
		{
			return;
		}
		register_classes_function();
	}

	void SysModule::UnregisterFactory()
	{
		if (!unregister_classes_function)
		{
			return;
		}
		unregister_classes_function();
	}

	bool SysModule::IsLoaded() const
	{
		return sysmodule_ != nullptr;
	}

	void SysModule::InvalidateReferences()
	{
		// Les acteurs (et leurs vtables), les composants, les comportements...
		// sont du code de la dll. Le niveau est supprime ici, tant que la dll est
		// encore chargee, pour qu'aucun objet vivant ne pointe dans une dll dechargee.
		Engine* engine = GetEngine();

		if (engine && engine->GetCurrentLevel())
		{
			engine->DeleteCurrentLevel();
		}

		// Ensuite seulement, on retire les classes de la factory.
		UnregisterFactory();
	}

#ifdef _WIN32
	void SysModule::Unload()
	{
		if (!sysmodule_)
		{
			return;
		}

		InvalidateReferences();

		FreeLibrary(sysmodule_);

		sysmodule_ = nullptr;
		register_classes_function = nullptr;
		unregister_classes_function = nullptr;
	}
#else
	void SysModule::Unload()
	{
	}
#endif
}
