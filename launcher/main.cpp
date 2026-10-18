// Lynx launcher : fenêtre Raven (Vulkan), mêmes thèmes et widgets que l'éditeur.
// L'interface (interface/launcher.xml) et celle de l'éditeur qu'elle réutilise
// (src/editor/interface : widgets.xml, themes/, icônes, police) sont dans
// l'exécutable (cmake/LynxEmbedFiles.cmake) : rien à livrer à côté.

#include "Launcher.h"
#include "../src/editor/raven/RavenUi.h"
#include "../src/editor/EditorOptions.h"

#include <Raven.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#endif

namespace ui = lynx::editor::ui;

namespace
{
	bool g_quit = false;

	int OnCloseRequest(Rid, void*)
	{
		g_quit = true;
		return R_FALSE;   // détruite à la fin de main
	}

	// Dossier de plugin ou .zip glissé sur la fenêtre : import (onglet Plugins).
	int OnDropFiles(Rid, const RDropEvent* event, void* user)
	{
		if (!event || event->path_count <= 0 || !event->paths)
			return R_FALSE;
		if (event->phase != R_DROP_DROPPED)
			return R_TRUE;   // accepté au survol
		std::vector<std::string> paths;
		for (int i = 0; i < event->path_count; ++i)
			if (event->paths[i])
				paths.emplace_back(event->paths[i]);
		static_cast<Launcher*>(user)->OnDrop(paths);
		return R_TRUE;
	}
}

int main(int, char**)
{
	RInitDesc desc;
	std::memset(&desc, 0, sizeof(desc));
	desc.app_name = "Lynx Launcher";
	if (!Rinit_ex(&desc))
	{
#ifdef _WIN32
		MessageBoxW(nullptr, L"Le launcher ne peut pas démarrer : Vulkan n'est pas disponible sur cet ordinateur "
		                     L"(mets à jour le pilote de la carte graphique).", L"Lynx Launcher", MB_OK | MB_ICONERROR);
#endif
		std::fprintf(stderr, "[UI] Rinit failed (Vulkan)\n");
		return 1;
	}

	ui::Init();                          // fichiers intégrés, <Icon>, thèmes
	lynx::editor::options::Init();       // thème et échelle choisis dans l'éditeur

	const Rid window = ui::LoadWindow("launcher.xml");
	if (window == R_INVALID_ID)
	{
		ui::Shutdown();
		Rshutdown();
		return 1;
	}

	{
		Launcher launcher(window);
		Rwindow_set_close_callback(window, OnCloseRequest, nullptr);
		Rwindow_set_drop_callback(window, OnDropFiles, &launcher);
		launcher.Tick();   // première page remplie avant d'afficher la fenêtre
		Rwindow_show(window);

		// Les barres de progression et le journal bougent pendant les tâches :
		// on se réveille au moins 10 fois par seconde.
		while (!g_quit && Rwait_frame(0.1))
			launcher.Tick();

		Rwindow_set_drop_callback(window, nullptr, nullptr);
		Rwindow_set_close_callback(window, nullptr, nullptr);
	}

	Rwindow_destroy(window);
	ui::Shutdown();
	Rshutdown();
	return 0;
}
