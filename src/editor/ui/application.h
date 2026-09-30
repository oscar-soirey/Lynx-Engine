#pragma once

#include "../common.h"
#include "../utils/editor_dispatcher.h"

#include <string>


class QApplication;

namespace hn::editor
{
	class EDITOR_API EditorApplication {
	public:
		EditorApplication(int argc, char *argv[]);
		QApplication* GetApplication();

		std::string GetFullMessageConsole();
		void AppendMessage(const char* msg);

		EditorDispatcher<> ED_MessageReceived;

	private:
		QApplication* app_=nullptr;

		std::string app_messages_;
	};
}