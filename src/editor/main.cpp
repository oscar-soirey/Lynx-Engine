#include <iostream>
#include <qcoreapplication.h>

#include "common.h"
#include "../core/engine.h"
#include "ui/application.h"
#include "window/main_window.h"
#include "widgets/viewport.h"




int main(int argc, char *argv[])
{
	auto* main_window = new hn::editor::EditorMain();
	hn::editor::InitSetMainWindow(main_window);
	main_window->show();

	lynx::Engine engine("", false);

	while (!hn::editor::GetMainWindow()->ShouldClose())
	{
		QCoreApplication::processEvents();
		engine.ProgressOneFrame();
	}

	delete main_window;

	return 0;
}