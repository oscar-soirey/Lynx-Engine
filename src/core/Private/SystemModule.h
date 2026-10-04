#pragma once

#ifndef HGE_SYS_MODULE
#define HGE_SYS_MODULE

#include "../Common.h"

#include <string>

#ifdef _WIN32
#include <Windows.h>
#define SYSTEM_MODULE HMODULE
#elif defined(__linux__)
#define SYSTEM_MODULE
#elif defined(__APPLE__)
#define SYSTEM_MODULE
#endif


namespace lynx
{
	class LYNX_API SysModule {
	public:
		explicit SysModule(const char* shared_file_path);
		~SysModule();

		// Un module possede la bibliotheque chargee : pas de copie.
		SysModule(const SysModule&) = delete;
		SysModule& operator=(const SysModule&) = delete;

		void RegisterFactory();
		void UnregisterFactory();

		// true tant que la bibliotheque est chargee.
		bool IsLoaded() const;

		// Adresse d'une fonction exportee par la dll (nullptr si absente ou si
		// la dll n'est pas chargee). Voir core/GameModuleAPI.h.
		void* GetSymbol(const char* name) const;

		// Chemin donne au constructeur.
		const std::string& GetPath() const { return path_; }

		// Invalide tout ce qui vient de la bibliotheque, puis la decharge :
		//   1. supprime le niveau courant de l'engine (detruit tous les acteurs
		//      pendant que le code de la dll est encore mappe),
		//   2. desenregistre les classes de la factory,
		//   3. FreeLibrary.
		// Appele par le destructeur. Peut etre appele plusieurs fois.
		void Unload();

	private:
		std::string path_;

		// Ce qui doit etre invalide avant de decharger la dll.
		void InvalidateReferences();

#ifdef _WIN32
		SYSTEM_MODULE sysmodule_ = nullptr;
#else
		SYSTEM_MODULE sysmodule_;
#endif

		using RegisterFactoryFn = void (*)();
		RegisterFactoryFn register_classes_function = nullptr;

		using UnregisterFactoryFn = void (*)();
		UnregisterFactoryFn unregister_classes_function = nullptr;
	};
}

#endif
