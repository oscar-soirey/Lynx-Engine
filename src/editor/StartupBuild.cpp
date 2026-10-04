#include "StartupBuild.h"

#include "GameBuild.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include <glfw/glfw3.h>

#include <algorithm>
#include <iostream>
#include <system_error>

namespace fs = std::filesystem;

namespace lynx::editor
{
	namespace
	{
		enum class State
		{
			Building,
			Failed,
		};

		float ButtonWidth(const char* label)
		{
			return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.f;
		}
	}


	StartupBuildResult RunStartupBuild(
		const host::GameProject& project,
		const std::string& reason,
		bool allow_open_anyway)
	{
		std::cout << "[BUILD] " << reason << "\n"
		          << "[BUILD] Compiling " << project.name << " before opening it\n";

		if (!glfwInit())
		{
			std::cerr << "[BUILD] glfwInit failed\n";
			return StartupBuildResult::Quit;
		}

		glfwDefaultWindowHints();
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		const std::string title = "Lynx Engine - Compiling " + project.name;
		GLFWwindow* window = glfwCreateWindow(1000, 600, title.c_str(), nullptr, nullptr);

		if (!window)
		{
			std::cerr << "[BUILD] Could not create the build window\n";
			return StartupBuildResult::Quit;
		}

		glfwMakeContextCurrent(window);
		glfwSwapInterval(1);

		IMGUI_CHECKVERSION();
		ImGuiContext* context = ImGui::CreateContext();
		ImGui::SetCurrentContext(context);

		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;

		const fs::path font = host::GetEditorDirectory() / "normal-font.ttf";
		std::error_code font_error;

		if (fs::is_regular_file(font, font_error))
			io.Fonts->AddFontFromFileTTF(font.string().c_str(), 20.f);

		ImGui::StyleColorsDark();
		ImGui::GetStyle().FrameRounding = 4.f;
		ImGui::GetStyle().FramePadding = ImVec2(8.f, 5.f);

		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL3_Init("#version 330");

		State state = State::Building;
		StartupBuildResult result = StartupBuildResult::Quit;
		bool done = false;

		if (!game_build::Start())
			state = State::Failed;

		while (!done)
		{
			glfwPollEvents();

			// Window closed : stop the build and quit.
			if (glfwWindowShouldClose(window))
			{
				game_build::Shutdown();
				result = StartupBuildResult::Quit;
				break;
			}

			bool success = false;

			if (state == State::Building && game_build::PollFinished(success))
			{
				if (success)
				{
					// The editor's "Build" window does not need to pop up.
					game_build::WindowOpen() = false;
					result = StartupBuildResult::Built;
					done = true;
				}
				else
				{
					state = State::Failed;
				}
			}

			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			const ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->WorkPos);
			ImGui::SetNextWindowSize(viewport->WorkSize);

			ImGui::Begin("##StartupBuild", nullptr,
			             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
			             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

			ImGui::Text("Project : %s", project.name.c_str());
			ImGui::TextDisabled("%s", project.root.string().c_str());
			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.80f, 0.35f, 1.f));
			ImGui::TextWrapped("%s", reason.c_str());
			ImGui::PopStyleColor();

			ImGui::Spacing();

			if (state == State::Building)
			{
				const std::string status = game_build::ToolbarStatus();
				ImGui::Text("%s", status.empty() ? "Building..." : status.c_str());
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1.f));
				ImGui::TextWrapped(
					allow_open_anyway
						? "The build failed. Fix the errors and retry, or open the project with the "
						  "previous build of the game."
						: "The build failed. Fix the errors and retry : the current game DLL cannot "
						  "be loaded by this engine (it would crash the editor).");
				ImGui::PopStyleColor();
			}

			ImGui::Spacing();

			// Output, then the buttons on the last line.
			const ImGuiStyle& style = ImGui::GetStyle();
			const float buttons_height = ImGui::GetFrameHeight() + style.ItemSpacing.y * 2.f;

			ImGui::BeginChild("##Log", ImVec2(0.f, -buttons_height));
			game_build::DrawOutput();
			ImGui::EndChild();

			ImGui::Spacing();

			if (state == State::Building)
			{
				ImGui::SetCursorPosX(
					ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ButtonWidth("Cancel"));

				if (ImGui::Button("Cancel"))
					game_build::Cancel();   // ends as a failed build
			}
			else
			{
				if (ImGui::Button("Retry"))
				{
					if (game_build::Start())
						state = State::Building;
				}

				ImGui::SameLine();

				if (allow_open_anyway && ImGui::Button("Open with the previous build"))
				{
					result = StartupBuildResult::OpenAnyway;
					done = true;
				}

				const float right =
					ButtonWidth("Other project...") + ButtonWidth("Quit") + style.ItemSpacing.x;

				ImGui::SameLine();
				ImGui::SetCursorPosX(
					ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - right));

				if (ImGui::Button("Other project..."))
				{
					result = StartupBuildResult::OtherProject;
					done = true;
				}

				ImGui::SameLine();

				if (ImGui::Button("Quit"))
				{
					result = StartupBuildResult::Quit;
					done = true;
				}
			}

			ImGui::End();

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
		}

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext(context);

		glfwDestroyWindow(window);
		glfwDefaultWindowHints();

		return result;
	}
}
