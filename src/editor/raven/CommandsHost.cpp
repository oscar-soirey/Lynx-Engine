#include "CommandsHost.h"

#include "../commands/CommandsWindow.h"
#include "../EditorFonts.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include <glfw/glfw3.h>

#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace lynx::editor::commands_host
{
	namespace
	{
		GLFWwindow* g_window = nullptr;
		ImGuiContext* g_imgui = nullptr;
		bool g_failed = false;
		bool g_shown = false;

		// The OpenGL context of the editor (gl::Init), current again after drawing.
		struct SavedContext
		{
#ifdef _WIN32
			HDC dc = nullptr;
			HGLRC rc = nullptr;
#endif
			ImGuiContext* imgui = nullptr;

			void Save()
			{
#ifdef _WIN32
				dc = wglGetCurrentDC();
				rc = wglGetCurrentContext();
#endif
				imgui = ImGui::GetCurrentContext();
			}

			void Restore() const
			{
#ifdef _WIN32
				wglMakeCurrent(dc, rc);
#endif
				ImGui::SetCurrentContext(imgui);
			}
		};

		void ApplyStyle()
		{
			ImGui::StyleColorsDark();
			ImGuiStyle& style = ImGui::GetStyle();
			style.WindowRounding = 0.f;
			style.WindowBorderSize = 0.f;
			style.FramePadding = ImVec2(8, 5);
			style.ItemSpacing = ImVec2(8, 6);
			style.ScrollbarSize = 12;

			ImVec4* c = style.Colors;
			c[ImGuiCol_WindowBg]       = ImVec4(0.105f, 0.11f, 0.13f, 1);
			c[ImGuiCol_ChildBg]        = ImVec4(0, 0, 0, 0);
			c[ImGuiCol_Border]         = ImVec4(0.20f, 0.21f, 0.25f, 1);
			c[ImGuiCol_FrameBg]        = ImVec4(0.16f, 0.17f, 0.20f, 1);
			c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.21f, 0.25f, 1);
			c[ImGuiCol_Button]         = ImVec4(0.19f, 0.20f, 0.24f, 1);
			c[ImGuiCol_ButtonHovered]  = ImVec4(0.25f, 0.26f, 0.31f, 1);
			c[ImGuiCol_ButtonActive]   = ImVec4(0.30f, 0.31f, 0.36f, 1);
			c[ImGuiCol_CheckMark]      = ImVec4(0.96f, 0.56f, 0.20f, 1);
			c[ImGuiCol_Tab]            = ImVec4(0.16f, 0.17f, 0.20f, 1);
			c[ImGuiCol_TabHovered]     = ImVec4(0.30f, 0.31f, 0.36f, 1);
			c[ImGuiCol_TabSelected]    = ImVec4(0.25f, 0.26f, 0.31f, 1);
		}

		bool Create()
		{
			if (g_window)
				return true;
			if (g_failed)
				return false;

			SavedContext saved;
			saved.Save();

			glfwDefaultWindowHints();
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			g_window = glfwCreateWindow(1000, 640, "Lynx - Commands", nullptr, nullptr);
			glfwDefaultWindowHints();
			if (!g_window)
			{
				std::cerr << "[EDITOR] Could not create the Commands window (GLFW / OpenGL 3.3)\n";
				g_failed = true;
				saved.Restore();
				return false;
			}
			glfwSetWindowSizeLimits(g_window, 480, 300, GLFW_DONT_CARE, GLFW_DONT_CARE);

			glfwMakeContextCurrent(g_window);
			glfwSwapInterval(0);   // never makes the editor wait for the screen

			g_imgui = ImGui::CreateContext();
			ImGui::SetCurrentContext(g_imgui);
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			ApplyStyle();
			fonts::Load(22.f);
			ImGui_ImplGlfw_InitForOpenGL(g_window, true);
			ImGui_ImplOpenGL3_Init("#version 330");

			saved.Restore();
			return true;
		}

		void Draw(bool& open)
		{
			SavedContext saved;
			saved.Save();

			glfwMakeContextCurrent(g_window);
			ImGui::SetCurrentContext(g_imgui);

			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			// The ImGui window fills the native window (its title bar stays : its X closes it).
			const ImVec2 size = ImGui::GetIO().DisplaySize;
			commands_window::Draw(&open);
			ImGui::SetWindowPos("Commands", ImVec2(0.f, 0.f), ImGuiCond_Always);
			ImGui::SetWindowSize("Commands", size, ImGuiCond_Always);

			ImGui::Render();
			int w = 0, h = 0;
			glfwGetFramebufferSize(g_window, &w, &h);
			glViewport(0, 0, w, h);
			glClearColor(0.105f, 0.11f, 0.13f, 1.f);
			glClear(GL_COLOR_BUFFER_BIT);
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			glfwSwapBuffers(g_window);

			saved.Restore();
		}
	}


	void Frame(bool& open)
	{
		if (!open)
		{
			if (g_window && g_shown)
			{
				glfwHideWindow(g_window);
				g_shown = false;
			}
			return;
		}

		if (!Create())
		{
			open = false;
			return;
		}

		if (!g_shown)
		{
			glfwSetWindowShouldClose(g_window, GLFW_FALSE);
			glfwShowWindow(g_window);
			glfwFocusWindow(g_window);
			g_shown = true;
		}

		// The X of the native window.
		if (glfwWindowShouldClose(g_window))
		{
			open = false;
			glfwHideWindow(g_window);
			g_shown = false;
			return;
		}

		// Minimized : nothing to draw.
		if (glfwGetWindowAttrib(g_window, GLFW_ICONIFIED))
			return;

		Draw(open);
	}

	void Shutdown()
	{
		if (!g_window)
			return;

		SavedContext saved;
		saved.Save();

		glfwMakeContextCurrent(g_window);
		ImGui::SetCurrentContext(g_imgui);
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext(g_imgui);
		g_imgui = nullptr;
		glfwDestroyWindow(g_window);
		g_window = nullptr;
		g_shown = false;

		saved.imgui = nullptr;
		saved.Restore();
	}
}
