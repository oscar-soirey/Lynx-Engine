#include "ProjectBrowser.h"
#include "ProjectTemplates.h"

#include "raven/RavenUi.h"

#include <Raven.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#endif

// =============================================================================
// Project browser in Raven (interface/windows/project_browser.xml and
// new_project.xml). Runs its own loop of Rwait_frame until a project is
// opened or the window is closed ; Raven stays initialized afterwards.
// =============================================================================

namespace fs = std::filesystem;

namespace lynx::editor
{
	using namespace lynx::host;

	namespace
	{
		// ---------------------------------------------------------------------
		// Native folder dialog (Windows). ole32 is loaded at runtime so the
		// editor does not need any extra library at link time.
		// ---------------------------------------------------------------------
		bool BrowseForFolder(fs::path& out)
		{
#ifdef _WIN32
			HMODULE ole32 = LoadLibraryW(L"ole32.dll");

			if (!ole32)
				return false;

			using CoInitializeExFn = HRESULT (WINAPI*)(LPVOID, DWORD);
			using CoUninitializeFn = void (WINAPI*)();
			using CoCreateInstanceFn = HRESULT (WINAPI*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
			using CoTaskMemFreeFn = void (WINAPI*)(LPVOID);

			auto co_initialize = reinterpret_cast<CoInitializeExFn>(
				reinterpret_cast<void*>(GetProcAddress(ole32, "CoInitializeEx")));
			auto co_uninitialize = reinterpret_cast<CoUninitializeFn>(
				reinterpret_cast<void*>(GetProcAddress(ole32, "CoUninitialize")));
			auto co_create_instance = reinterpret_cast<CoCreateInstanceFn>(
				reinterpret_cast<void*>(GetProcAddress(ole32, "CoCreateInstance")));
			auto co_task_mem_free = reinterpret_cast<CoTaskMemFreeFn>(
				reinterpret_cast<void*>(GetProcAddress(ole32, "CoTaskMemFree")));

			if (!co_initialize || !co_uninitialize || !co_create_instance || !co_task_mem_free)
			{
				FreeLibrary(ole32);
				return false;
			}

			// CLSID_FileOpenDialog / IID_IFileOpenDialog (defined here so uuid.lib
			// is not needed).
			static const CLSID kClsidFileOpenDialog =
				{ 0xDC1C5A9C, 0xE88A, 0x4DDE, { 0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7 } };
			static const IID kIidFileOpenDialog =
				{ 0xD57C7288, 0xD4AD, 0x4768, { 0xBE, 0x02, 0x9D, 0x96, 0x95, 0x32, 0xD9, 0x60 } };

			const HRESULT init = co_initialize(
				nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

			bool picked = false;
			IFileOpenDialog* dialog = nullptr;

			if (SUCCEEDED(co_create_instance(
					kClsidFileOpenDialog,
					nullptr,
					CLSCTX_INPROC_SERVER,
					kIidFileOpenDialog,
					reinterpret_cast<void**>(&dialog))) && dialog)
			{
				DWORD options = 0;
				dialog->GetOptions(&options);
				dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
				dialog->SetTitle(L"Select a Lynx game project folder");

				if (SUCCEEDED(dialog->Show(GetActiveWindow())))
				{
					IShellItem* item = nullptr;

					if (SUCCEEDED(dialog->GetResult(&item)) && item)
					{
						PWSTR path = nullptr;

						if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
						{
							out = fs::path(path);
							picked = true;
							co_task_mem_free(path);
						}

						item->Release();
					}
				}

				dialog->Release();
			}

			if (SUCCEEDED(init))
				co_uninitialize();

			FreeLibrary(ole32);
			return picked;
#else
			(void)out;
			return false;
#endif
		}



		struct RecentEntry
		{
			fs::path root;
			std::string name;
			std::string path_text;
			bool available = false;   // assets/ and build/ exist (cheap check)
		};

		struct Browser
		{
			Rid window = R_INVALID_ID;
			Rid recent_view = R_INVALID_ID;
			Rid path = R_INVALID_ID;
			Rid status = R_INVALID_ID;
			Rid module_label = R_INVALID_ID;
			Rid module = R_INVALID_ID;
			Rid open = R_INVALID_ID;
			Rid message = R_INVALID_ID;

			std::vector<RecentEntry> recent;
			std::vector<std::string> recent_labels;   // texts given to the tiles

			std::string inspected_text = "\x01";   // never equal at the start
			bool inspected_ok = false;
			GameProject inspected;
			std::string inspected_error;
			int module_index = 0;

			// "New project" dialog
			std::string new_title = "My Game";
			std::string new_location;
			int template_index = 0;

			bool open_requested = false;
			bool quit_requested = false;
		};

		std::string Relative(const fs::path& p, const fs::path& root)
		{
			std::error_code error;
			const fs::path r = fs::relative(p, root, error);
			return (error || r.empty()) ? p.generic_string() : r.generic_string();
		}

		void RefreshRecent(Browser& b)
		{
			b.recent.clear();
			for (const fs::path& root : LoadRecentProjects())
			{
				RecentEntry entry;
				entry.root = root;
				entry.name = root.filename().string();
				entry.path_text = root.string();
				std::error_code error;
				entry.available = fs::is_directory(root / "assets", error) && fs::is_directory(root / "build", error);
				b.recent.push_back(std::move(entry));
			}

			b.recent_labels.clear();
			for (const RecentEntry& e : b.recent)
				b.recent_labels.push_back((e.name.empty() ? e.path_text : e.name) + (e.available ? "" : "   (not found)"));

			ui::OnTileItem(b.recent_view, static_cast<int>(b.recent.size()), [&b](int i, RTileItem& item)
			{
				const RecentEntry& e = b.recent[static_cast<size_t>(i)];
				item.text = b.recent_labels[static_cast<size_t>(i)].c_str();
				item.detail = e.path_text.c_str();
				item.icon = ui::FileIconTexture();
				ui::FileIconRegion(file_icons::Kind::Folder, item.uv);
				if (!e.available)
				{
					item.text_color[0] = 0.86f; item.text_color[1] = 0.43f; item.text_color[2] = 0.43f; item.text_color[3] = 1.f;
				}
			});
		}

		// Re-inspects the folder of the path field when it changed.
		void UpdateInspection(Browser& b)
		{
			const std::string text = Rtext_input_get_text(b.path) ? Rtext_input_get_text(b.path) : "";
			if (text == b.inspected_text)
				return;

			b.inspected_text = text;
			b.module_index = 0;
			b.inspected_ok = !text.empty() && InspectProject(fs::path(text), b.inspected, b.inspected_error);

			ui::SetClass(b.status, "error", false);
			ui::SetClass(b.status, "warning", false);
			ui::SetClass(b.status, "dim", false);
			ui::SetVisible(b.module_label, false);
			ui::SetVisible(b.module, false);

			if (text.empty())
			{
				ui::SetText(b.status, "Select a project folder.");
				ui::SetClass(b.status, "dim", true);
			}
			else if (!b.inspected_ok)
			{
				ui::SetText(b.status, b.inspected_error);
				ui::SetClass(b.status, "error", true);
			}
			else
			{
				const std::vector<fs::path>& modules = b.inspected.modules;
				if (modules.empty())
				{
					ui::SetText(b.status, "Game DLL : not built yet. It will be compiled when the project opens.");
					ui::SetClass(b.status, "warning", true);
				}
				else if (modules.size() == 1)
				{
					ui::SetText(b.status, "Game DLL : " + Relative(modules.front(), b.inspected.root));
				}
				else
				{
					ui::SetText(b.status, "");
					Rcombo_box_clear_items(b.module);
					for (const fs::path& m : modules)
						Rcombo_box_add_item(b.module, Relative(m, b.inspected.root).c_str());
					Rcombo_box_set_selected(b.module, 0);
					ui::SetVisible(b.module_label, true);
					ui::SetVisible(b.module, true);
				}
			}

			ui::SetEnabled(b.open, b.inspected_ok);

			// The recent project of the field is the selected tile.
			Rtile_view_clear_selection(b.recent_view);
			for (size_t i = 0; i < b.recent.size(); ++i)
				if (b.recent[i].path_text == text)
					Rtile_view_set_selected(b.recent_view, static_cast<int>(i), R_TRUE);
		}

		void SetPath(Browser& b, const std::string& text)
		{
			Rtext_input_set_text(b.path, text.c_str());
			UpdateInspection(b);
		}

		void TryOpen(Browser& b)
		{
			UpdateInspection(b);
			if (b.inspected_ok)
				b.open_requested = true;
		}

		// Where new projects go by default : next to the last project, or
		// Documents/Lynx Projects.
		fs::path DefaultProjectsFolder(const Browser& b)
		{
			if (!b.recent.empty() && b.recent.front().root.has_parent_path())
				return b.recent.front().root.parent_path();
			const char* home = std::getenv("USERPROFILE");
			if (!home)
				home = std::getenv("HOME");
			return (home ? fs::path(home) / "Documents" : fs::current_path()) / "Lynx Projects";
		}

		void OpenNewProjectDialog(Browser& b)
		{
			Rid box = ui::Load("windows/new_project.xml");
			if (box == R_INVALID_ID)
				return;

			struct Dialog
			{
				std::vector<project_templates::Template> templates;
				std::string templates_error;
				std::vector<std::string> labels;
			};
			auto dialog = std::make_shared<Dialog>();
			dialog->templates = project_templates::List(dialog->templates_error);
			b.template_index = std::clamp(b.template_index, 0, std::max(0, static_cast<int>(dialog->templates.size()) - 1));
			if (b.new_location.empty())
				b.new_location = DefaultProjectsFolder(b).string();

			Rid title = ui::Find(box, "np.title");
			Rid location = ui::Find(box, "np.location");
			Rid folder = ui::Find(box, "np.folder");
			Rid dll = ui::Find(box, "np.dll");
			Rid tiles = ui::Find(box, "np.templates");
			Rid error = ui::Find(box, "np.error");
			Rid create = ui::Find(box, "np.create");

			Rtext_input_set_text(title, b.new_title.c_str());
			Rtext_input_set_text(location, b.new_location.c_str());

			auto update = [&b, dialog, folder, dll, create]()
			{
				const std::string identifier = project_templates::ToIdentifier(b.new_title);
				const fs::path target = project_templates::ProjectFolder(fs::path(b.new_location), b.new_title);
				const bool named = !identifier.empty() && !target.empty();
				ui::SetText(folder, named ? "Folder : " + target.string() : "Type a name (letters or digits).");
				ui::SetText(dll, named ? "Game DLL : " + identifier + ".dll" : "");
				ui::SetEnabled(create, named && !dialog->templates.empty());
			};

			if (dialog->templates.empty())
			{
				ui::SetText(error, dialog->templates_error);
				ui::SetVisible(error, true);
			}

			ui::OnTileItem(tiles, static_cast<int>(dialog->templates.size()), [dialog](int i, RTileItem& item)
			{
				const project_templates::Template& t = dialog->templates[static_cast<size_t>(i)];
				item.text = t.name.c_str();
				item.detail = t.description.c_str();
				if (!t.icon.empty())
					item.icon = ui::FileTexture(t.icon);
			});
			if (!dialog->templates.empty())
				Rtile_view_set_selected(tiles, b.template_index, R_TRUE);

			auto do_create = [&b, dialog, box, error, update]()
			{
				update();
				if (dialog->templates.empty())
					return;
				const auto& tmpl = dialog->templates[static_cast<size_t>(b.template_index)];
				fs::path root;
				std::string create_error;
				if (!project_templates::Create(tmpl, fs::path(b.new_location), b.new_title, root, create_error))
				{
					ui::SetText(error, create_error);
					ui::SetVisible(error, true);
					return;
				}
				SetPath(b, root.string());
				if (b.inspected_ok)
					b.open_requested = true;
				else
				{
					ui::SetText(b.message, "The project was created, but cannot be opened : " + b.inspected_error);
					ui::SetVisible(b.message, true);
				}
				b.new_title = "My Game";
				Rdialog_close(box, 1);
			};

			ui::OnText(title, [&b, update](const std::string& text) { b.new_title = text; update(); });
			ui::OnCommit(title, [&b, update](const std::string& text) { b.new_title = text; update(); });
			ui::OnText(location, [&b, update](const std::string& text) { b.new_location = text; update(); });
			ui::OnClick(ui::Find(box, "np.browse"), [&b, location, update]()
			{
				fs::path picked;
				if (BrowseForFolder(picked))
				{
					b.new_location = picked.string();
					Rtext_input_set_text(location, b.new_location.c_str());
					update();
				}
			});
			ui::OnTileSelect(tiles, [&b](int index) { if (index >= 0) b.template_index = index; });
			ui::OnTileActivate(tiles, [&b, do_create](int index) { b.template_index = index; do_create(); });
			ui::OnClick(create, do_create);
			ui::OnClick(ui::Find(box, "np.cancel"), [box]() { Rdialog_close(box, 0); });

			update();
			Rdialog_open(b.window, box, nullptr, nullptr);
			Rwidget_focus(title);
			Rtext_input_select_all(title);
		}

		int OnDropFiles(Rid, const RDropEvent* event, void* user)
		{
			Browser& b = *static_cast<Browser*>(user);
			if (event->path_count <= 0 || !event->paths || !event->paths[0])
				return R_FALSE;
			if (event->phase != R_DROP_DROPPED)
				return R_TRUE;

			fs::path dropped(event->paths[0]);
			std::error_code error;
			// A file dropped from the project (ex : the .dll) -> its folder.
			if (!fs::is_directory(dropped, error))
				dropped = dropped.parent_path();
			// Dropped "build" or "assets" -> the project folder.
			const std::string leaf = dropped.filename().string();
			if (leaf == "build" || leaf == "assets")
				dropped = dropped.parent_path();

			SetPath(b, dropped.string());
			return R_TRUE;
		}

		int OnCloseRequest(Rid, void* user)
		{
			static_cast<Browser*>(user)->quit_requested = true;
			return R_FALSE;   // destroyed by RunProjectBrowser
		}
	}


	bool RunProjectBrowser(
		GameProject& project,
		const std::string& message,
		const fs::path& initial_path)
	{
		Browser b;
		b.window = ui::LoadWindow("windows/project_browser.xml");
		if (b.window == R_INVALID_ID)
		{
			std::cerr << "[PROJECT] Could not create the project browser window\n";
			return false;
		}

		b.recent_view = ui::Find(b.window, "pb.recent");
		b.path = ui::Find(b.window, "pb.path");
		b.status = ui::Find(b.window, "pb.status");
		b.module_label = ui::Find(b.window, "pb.module.label");
		b.module = ui::Find(b.window, "pb.module");
		b.open = ui::Find(b.window, "pb.open");
		b.message = ui::Find(b.window, "pb.message");

		Rimage_set_texture(ui::Find(b.window, "pb.logo"), ui::Texture("icons/lynxie.png"));

		if (!message.empty())
		{
			ui::SetText(b.message, message);
			ui::SetVisible(b.message, true);
		}

		Rwindow_set_close_callback(b.window, OnCloseRequest, &b);
		Rwindow_set_drop_callback(b.window, OnDropFiles, &b);

		RefreshRecent(b);

		ui::OnText(b.path, [&b](const std::string&) { UpdateInspection(b); });
		ui::OnCommit(b.path, [&b](const std::string&) { TryOpen(b); });
		ui::OnSelect(b.module, [&b](int index) { b.module_index = std::max(0, index); });
		ui::OnClick(ui::Find(b.window, "pb.browse"), [&b]()
		{
			fs::path picked;
			if (BrowseForFolder(picked))
				SetPath(b, picked.string());
		});
		ui::OnClick(ui::Find(b.window, "pb.new"), [&b]() { OpenNewProjectDialog(b); });
		ui::OnClick(ui::Find(b.window, "pb.quit"), [&b]() { b.quit_requested = true; });
		ui::OnClick(b.open, [&b]() { TryOpen(b); });

		ui::OnTileSelect(b.recent_view, [&b](int index)
		{
			if (index >= 0 && index < static_cast<int>(b.recent.size()))
				SetPath(b, b.recent[static_cast<size_t>(index)].path_text);
		});
		ui::OnTileActivate(b.recent_view, [&b](int index)
		{
			if (index < 0 || index >= static_cast<int>(b.recent.size()))
				return;
			SetPath(b, b.recent[static_cast<size_t>(index)].path_text);
			TryOpen(b);
		});

		ui::Menu menu(b.window);
		ui::OnTileContext(b.recent_view, [&b, &menu](int index, float, float)
		{
			if (index < 0 || index >= static_cast<int>(b.recent.size()))
				return;
			const RecentEntry entry = b.recent[static_cast<size_t>(index)];
			menu.Clear();
			menu.Item("Open", [&b, entry]() { SetPath(b, entry.path_text); TryOpen(b); });
			menu.Item("Remove from list", [&b, entry]()
			{
				ui::Later([&b, entry]() { RemoveRecentProject(entry.root); RefreshRecent(b); });
			});
			menu.Open();
		});

		if (!initial_path.empty())
			SetPath(b, initial_path.string());
		else if (!b.recent.empty())
			SetPath(b, b.recent.front().path_text);
		else
			UpdateInspection(b);

		Rwindow_show(b.window);
		Rwidget_focus(b.path);

		bool opened = false;
		while (!b.quit_requested && Rwait_frame(0.25))
		{
			if (!b.open_requested)
				continue;
			b.open_requested = false;
			if (!b.inspected_ok)
				continue;

			project = b.inspected;
			if (!project.modules.empty())
			{
				const int index = std::clamp(b.module_index, 0, static_cast<int>(project.modules.size()) - 1);
				project.module_path = project.modules[static_cast<size_t>(index)];
			}
			opened = true;
			break;
		}

		Rwindow_set_close_callback(b.window, nullptr, nullptr);
		Rwindow_set_drop_callback(b.window, nullptr, nullptr);
		Rwindow_destroy(b.window);
		return opened;
	}
}
