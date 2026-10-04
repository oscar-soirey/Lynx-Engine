#include "ProjectBrowser.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include <glfw/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#endif

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


		// ---------------------------------------------------------------------
		// State
		// ---------------------------------------------------------------------

		struct RecentEntry
		{
			fs::path root;
			std::string name;
			std::string path_text;
			bool available = false;   // assets/ and build/ exist (cheap check)
		};

		struct BrowserState
		{
			std::vector<RecentEntry> recent;

			char path_buffer[1024] = {};

			// Result of InspectProject() for the text in path_buffer.
			std::string inspected_text;
			bool inspected_ok = false;
			GameProject inspected;
			std::string inspected_error;
			int module_index = 0;

			std::string message;

			bool open_requested = false;
			bool quit_requested = false;
		};

		BrowserState* g_state = nullptr;


		void SetPathText(BrowserState& state, const std::string& text)
		{
			std::snprintf(state.path_buffer, sizeof(state.path_buffer), "%s", text.c_str());
		}


		void RefreshRecent(BrowserState& state)
		{
			state.recent.clear();

			for (const fs::path& root : LoadRecentProjects())
			{
				RecentEntry entry;
				entry.root = root;
				entry.name = root.filename().string();
				entry.path_text = root.string();

				std::error_code error;
				entry.available =
					fs::is_directory(root / "assets", error) &&
					fs::is_directory(root / "build", error);

				state.recent.push_back(std::move(entry));
			}
		}


		// Re-inspects the folder typed in the path field when it changed.
		void UpdateInspection(BrowserState& state)
		{
			const std::string text = state.path_buffer;

			if (text == state.inspected_text)
				return;

			state.inspected_text = text;
			state.module_index = 0;

			if (text.empty())
			{
				state.inspected_ok = false;
				state.inspected_error.clear();
				return;
			}

			state.inspected_ok = InspectProject(fs::path(text), state.inspected, state.inspected_error);
		}


		void DropCallback(GLFWwindow*, int count, const char** paths)
		{
			if (!g_state || count <= 0 || !paths || !paths[0])
				return;

			fs::path dropped(paths[0]);
			std::error_code error;

			// A file dropped from the project (ex : the .dll) -> its folder.
			if (!fs::is_directory(dropped, error))
				dropped = dropped.parent_path();

			// Dropped "build" or "assets" -> the project folder.
			const std::string leaf = dropped.filename().string();

			if (leaf == "build" || leaf == "assets")
				dropped = dropped.parent_path();

			SetPathText(*g_state, dropped.string());
		}


		// Two-line row : project name, then its path (dimmed).
		bool RecentRow(const RecentEntry& entry, bool selected, bool& double_clicked)
		{
			const float line = ImGui::GetTextLineHeight();
			const ImGuiStyle& style = ImGui::GetStyle();

			ImGui::PushID(entry.path_text.c_str());

			const bool clicked = ImGui::Selectable(
				"##recent",
				selected,
				ImGuiSelectableFlags_AllowDoubleClick,
				ImVec2(0.f, line * 2.f + style.ItemSpacing.y));

			double_clicked = clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

			const ImVec2 min = ImGui::GetItemRectMin();
			ImDrawList* draw = ImGui::GetWindowDrawList();

			const ImU32 name_color = entry.available
				? ImGui::GetColorU32(ImGuiCol_Text)
				: IM_COL32(220, 110, 110, 255);

			std::string name = entry.name.empty() ? entry.path_text : entry.name;

			if (!entry.available)
				name += "   (not found)";

			draw->AddText(ImVec2(min.x + style.FramePadding.x, min.y), name_color, name.c_str());
			draw->AddText(
				ImVec2(min.x + style.FramePadding.x, min.y + line + style.ItemSpacing.y * 0.5f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled),
				entry.path_text.c_str());

			ImGui::PopID();
			return clicked;
		}


		void DrawBrowser(BrowserState& state)
		{
			const ImGuiViewport* viewport = ImGui::GetMainViewport();

			ImGui::SetNextWindowPos(viewport->WorkPos);
			ImGui::SetNextWindowSize(viewport->WorkSize);

			const ImGuiWindowFlags flags =
				ImGuiWindowFlags_NoDecoration |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoBringToFrontOnFocus;

			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.f, 20.f));
			ImGui::Begin("##ProjectBrowser", nullptr, flags);
			ImGui::PopStyleVar();

			ImGui::TextUnformatted("Lynx Engine");
			ImGui::TextDisabled("Open a game project : a folder with \"assets\" and the game DLL in \"build\".");
			ImGui::Spacing();

			if (!state.message.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.65f, 0.35f, 1.f));
				ImGui::TextWrapped("%s", state.message.c_str());
				ImGui::PopStyleColor();
				ImGui::Spacing();
			}

			ImGui::Separator();
			ImGui::Spacing();
			ImGui::TextUnformatted("Recent projects");

			// Space left for the bottom part (path field, status, buttons).
			const float bottom_height =
				ImGui::GetFrameHeightWithSpacing() * 4.f +
				ImGui::GetTextLineHeightWithSpacing() * 2.f;

			ImGui::BeginChild("##RecentList", ImVec2(0.f, -bottom_height));

			if (state.recent.empty())
			{
				ImGui::Spacing();
				ImGui::TextDisabled("No recent project. Use \"Browse...\" or drop a project folder on this window.");
			}

			const RecentEntry* to_open = nullptr;
			const RecentEntry* to_remove = nullptr;

			for (const RecentEntry& entry : state.recent)
			{
				const bool selected = (entry.path_text == state.path_buffer);
				bool double_clicked = false;

				if (RecentRow(entry, selected, double_clicked))
				{
					SetPathText(state, entry.path_text);

					if (double_clicked)
						to_open = &entry;
				}

				if (ImGui::BeginPopupContextItem(entry.path_text.c_str()))
				{
					if (ImGui::MenuItem("Open"))
					{
						SetPathText(state, entry.path_text);
						to_open = &entry;
					}

					if (ImGui::MenuItem("Remove from list"))
						to_remove = &entry;

					ImGui::EndPopup();
				}
			}

			ImGui::EndChild();

			if (to_open)
			{
				UpdateInspection(state);

				if (state.inspected_ok)
					state.open_requested = true;
			}

			if (to_remove)
			{
				RemoveRecentProject(to_remove->root);
				RefreshRecent(state);
			}

			// -----------------------------------------------------------------
			// Path field
			// -----------------------------------------------------------------

			ImGui::Spacing();
			ImGui::TextUnformatted("Project folder");

			const float browse_width =
				ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.f;

			ImGui::SetNextItemWidth(
				ImGui::GetContentRegionAvail().x - browse_width - ImGui::GetStyle().ItemSpacing.x);

			const bool enter_pressed = ImGui::InputText(
				"##ProjectPath",
				state.path_buffer,
				sizeof(state.path_buffer),
				ImGuiInputTextFlags_EnterReturnsTrue);

			ImGui::SameLine();

			if (ImGui::Button("Browse..."))
			{
				fs::path picked;

				if (BrowseForFolder(picked))
					SetPathText(state, picked.string());
			}

			UpdateInspection(state);

			// -----------------------------------------------------------------
			// Status : game DLL found, or why the folder is not a project
			// -----------------------------------------------------------------

			if (state.inspected_text.empty())
			{
				ImGui::TextDisabled("Select a project folder.");
			}
			else if (!state.inspected_ok)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.45f, 0.45f, 1.f));
				ImGui::TextWrapped("%s", state.inspected_error.c_str());
				ImGui::PopStyleColor();
			}
			else
			{
				const auto relative = [&](const fs::path& p)
				{
					std::error_code error;
					const fs::path r = fs::relative(p, state.inspected.root, error);
					return (error || r.empty()) ? p.generic_string() : r.generic_string();
				};

				const std::vector<fs::path>& modules = state.inspected.modules;

				state.module_index =
					modules.empty() ? 0 :
					std::clamp(state.module_index, 0, static_cast<int>(modules.size()) - 1);

				if (modules.empty())
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.80f, 0.35f, 1.f));
					ImGui::TextWrapped("Game DLL : not built yet. It will be compiled when the project opens.");
					ImGui::PopStyleColor();
				}
				else if (modules.size() == 1)
				{
					ImGui::Text("Game DLL : %s", relative(modules.front()).c_str());
				}
				else
				{
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted("Game DLL :");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(-1.f);

					const std::string preview = relative(modules[state.module_index]);

					if (ImGui::BeginCombo("##GameModule", preview.c_str()))
					{
						for (int i = 0; i < static_cast<int>(modules.size()); ++i)
						{
							const std::string label = relative(modules[i]);

							if (ImGui::Selectable(label.c_str(), i == state.module_index))
								state.module_index = i;
						}

						ImGui::EndCombo();
					}
				}
			}

			// -----------------------------------------------------------------
			// Buttons
			// -----------------------------------------------------------------

			ImGui::Spacing();

			const ImGuiStyle& style = ImGui::GetStyle();
			const float open_width = ImGui::CalcTextSize("Open project").x + style.FramePadding.x * 2.f;
			const float quit_width = ImGui::CalcTextSize("Quit").x + style.FramePadding.x * 2.f;

			ImGui::SetCursorPosX(
				ImGui::GetCursorPosX() +
				ImGui::GetContentRegionAvail().x - open_width - quit_width - style.ItemSpacing.x);

			if (ImGui::Button("Quit"))
				state.quit_requested = true;

			ImGui::SameLine();

			ImGui::BeginDisabled(!state.inspected_ok);

			if (ImGui::Button("Open project") || (enter_pressed && state.inspected_ok))
				state.open_requested = true;

			ImGui::EndDisabled();

			ImGui::End();
		}
	}


	bool RunProjectBrowser(
		GameProject& project,
		const std::string& message,
		const fs::path& initial_path)
	{
		if (!glfwInit())
		{
			std::cerr << "[PROJECT] glfwInit failed\n";
			return false;
		}

		glfwDefaultWindowHints();
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		GLFWwindow* window = glfwCreateWindow(960, 600, "Lynx Engine - Projects", nullptr, nullptr);

		if (!window)
		{
			std::cerr << "[PROJECT] Could not create the project browser window\n";
			return false;
		}

		glfwMakeContextCurrent(window);
		glfwSwapInterval(1);

		BrowserState state;
		state.message = message;
		g_state = &state;

		RefreshRecent(state);

		if (!initial_path.empty())
			SetPathText(state, initial_path.string());
		else if (!state.recent.empty())
			SetPathText(state, state.recent.front().path_text);

		glfwSetDropCallback(window, DropCallback);

		// ImGui : own context, destroyed before returning.
		IMGUI_CHECKVERSION();
		ImGuiContext* context = ImGui::CreateContext();
		ImGui::SetCurrentContext(context);

		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;

		// Same font as the editor when it sits next to the executable.
		const fs::path font = GetEditorDirectory() / "normal-font.ttf";
		std::error_code font_error;

		if (fs::is_regular_file(font, font_error))
			io.Fonts->AddFontFromFileTTF(font.string().c_str(), 20.f);

		ImGui::StyleColorsDark();
		ImGui::GetStyle().FrameRounding = 4.f;
		ImGui::GetStyle().FramePadding = ImVec2(8.f, 5.f);

		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL3_Init("#version 330");

		bool opened = false;

		while (!glfwWindowShouldClose(window) && !state.quit_requested)
		{
			glfwPollEvents();

			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			DrawBrowser(state);

			// No text selection cursor (I-beam).
			if (ImGui::GetMouseCursor() == ImGuiMouseCursor_TextInput)
				ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);

			ImGui::Render();

			int width = 0;
			int height = 0;
			glfwGetFramebufferSize(window, &width, &height);
			glViewport(0, 0, width, height);
			glClearColor(0.08f, 0.08f, 0.09f, 1.f);
			glClear(GL_COLOR_BUFFER_BIT);

			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

			glfwSwapBuffers(window);

			if (state.open_requested)
			{
				state.open_requested = false;

				if (state.inspected_ok)
				{
					project = state.inspected;

					if (!project.modules.empty())
					{
						const int index = std::clamp(
							state.module_index, 0, static_cast<int>(project.modules.size()) - 1);

						project.module_path = project.modules[index];
					}

					opened = true;
					break;
				}
			}
		}

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext(context);

		g_state = nullptr;

		glfwDestroyWindow(window);
		glfwDefaultWindowHints();

		return opened;
	}
}
