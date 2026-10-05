#include "Launcher.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include <glfw/glfw3.h>

#include <cstdio>
#include <filesystem>

#ifdef _WIN32
	#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
	fs::path ExecutableDirectory()
	{
#ifdef _WIN32
		wchar_t buffer[MAX_PATH];
		DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		if (length > 0 && length < MAX_PATH)
			return fs::path(buffer).parent_path();
#endif
		return fs::current_path();
	}

	void LoadFonts()
	{
		// Même police que l'éditeur (normal-font.ttf à côté de l'exécutable),
		// sinon une police système. ImGui 1.92 : les glyphes (accents compris)
		// sont chargés à la demande, plus besoin de plages.
		ImGuiIO& io = ImGui::GetIO();
		const fs::path candidates[] = {
			ExecutableDirectory() / "normal-font.ttf",
			fs::path("normal-font.ttf"),
			fs::path("C:/Windows/Fonts/segoeui.ttf"),
		};
		for (const fs::path& font : candidates)
		{
			std::error_code error;
			if (fs::is_regular_file(font, error))   // AddFontFromFileTTF asserte sur un fichier absent
			{
				io.Fonts->AddFontFromFileTTF(font.string().c_str(), 20.0f);
				return;
			}
		}
		io.Fonts->AddFontDefault();
	}

	void ApplyStyle()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		ImGui::StyleColorsDark();
		// Arrondis / bordures : on garde le style « pixel » du ImGui de third-party.
		style.FramePadding = ImVec2(10, 6);
		style.ItemSpacing = ImVec2(10, 8);
		style.CellPadding = ImVec2(8, 6);
		style.ScrollbarSize = 12;

		ImVec4* c = style.Colors;
		c[ImGuiCol_WindowBg]        = ImVec4(0.105f, 0.11f, 0.13f, 1);
		c[ImGuiCol_ChildBg]         = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_Text]            = ImVec4(0.92f, 0.92f, 0.94f, 1);
		c[ImGuiCol_Border]          = ImVec4(0.20f, 0.21f, 0.25f, 1);
		c[ImGuiCol_Separator]       = ImVec4(0.20f, 0.21f, 0.25f, 1);
		c[ImGuiCol_FrameBg]         = ImVec4(0.16f, 0.17f, 0.20f, 1);
		c[ImGuiCol_FrameBgHovered]  = ImVec4(0.20f, 0.21f, 0.25f, 1);
		c[ImGuiCol_FrameBgActive]   = ImVec4(0.24f, 0.25f, 0.29f, 1);
		c[ImGuiCol_Button]          = ImVec4(0.19f, 0.20f, 0.24f, 1);
		c[ImGuiCol_ButtonHovered]   = ImVec4(0.25f, 0.26f, 0.31f, 1);
		c[ImGuiCol_ButtonActive]    = ImVec4(0.30f, 0.31f, 0.36f, 1);
		c[ImGuiCol_CheckMark]       = ImVec4(0.96f, 0.56f, 0.20f, 1);
		c[ImGuiCol_TableHeaderBg]   = ImVec4(0.13f, 0.14f, 0.16f, 1);
		c[ImGuiCol_TableRowBg]      = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_TableRowBgAlt]   = ImVec4(1, 1, 1, 0.025f);
		c[ImGuiCol_TableBorderLight]= ImVec4(0.18f, 0.19f, 0.22f, 1);
	}
}

int main(int, char**)
{
	glfwSetErrorCallback([](int code, const char* text) { std::fprintf(stderr, "GLFW %d: %s\n", code, text); });
	if (!glfwInit())
		return 1;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	GLFWwindow* window = glfwCreateWindow(1080, 720, "Lynx Launcher", nullptr, nullptr);
	if (!window)
	{
		glfwTerminate();
		return 1;
	}
	glfwSetWindowSizeLimits(window, 820, 560, GLFW_DONT_CARE, GLFW_DONT_CARE);
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO().IniFilename = nullptr;
	ApplyStyle();
	LoadFonts();
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 330");

	{
		Launcher launcher;
		while (!glfwWindowShouldClose(window))
		{
			// Pas besoin de 60 i/s quand rien ne bouge, mais le journal et les
			// barres de progression doivent se rafraîchir : on réveille 10x/s.
			glfwWaitEventsTimeout(0.1);
			// Pas d'appel OpenGL direct : la fenêtre ImGui couvre tout l'écran (fond opaque)
			// et le backend OpenGL3 règle lui-même le viewport.

			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
			launcher.Draw();
			ImGui::Render();

			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			glfwSwapBuffers(window);
		}
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
