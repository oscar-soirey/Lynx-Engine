#include "SplashScreen.h"

#include "EditorFonts.h"

#include "WindowIcon.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include <glfw/glfw3.h>

// Implementation compiled in EditorMain.cpp (STB_IMAGE_IMPLEMENTATION).
#include "../../third-party/stb/stb_image.h"

#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F   // not in the Windows <GL/gl.h> (OpenGL 1.1)
#endif

namespace lynx::editor::splash
{
	namespace
	{
#include "resources/lynxie_png.inl"

		// Size at 100 % scale (window pixels). Scaled with the monitor DPI.
		constexpr float kWidth = 640.f;
		constexpr float kHeight = 360.f;

		constexpr double kMinVisibleSeconds = 1.0;   // no flash when loading is instant
		constexpr double kFadeInSeconds = 0.15;
		constexpr double kFillSeconds = 0.25;        // bar -> 100 % at the end, at least
		constexpr double kFadeOutSeconds = 0.25;
		constexpr int kEditorFramesBeforeFade = 2;   // the editor's first frames stay hidden

		// Dark pixel palette of the editor (imgui_draw.cpp, IMGUI_PIXEL_THEME_DARK).
		constexpr ImU32 Rgb(int r, int g, int b, int a = 255)
		{
			return IM_COL32(r, g, b, a);
		}

		constexpr ImU32 kOutline = Rgb(15, 15, 15);
		constexpr ImU32 kDeep = Rgb(30, 30, 30);
		constexpr ImU32 kBackground = Rgb(37, 37, 37);
		constexpr ImU32 kBevel = Rgb(61, 61, 61);
		constexpr ImU32 kText = Rgb(230, 230, 230);
		constexpr ImU32 kTextDim = Rgb(128, 128, 128);
		constexpr ImU32 kAccent = Rgb(217, 154, 43);
		constexpr ImU32 kAccentHot = Rgb(240, 185, 74);
		constexpr ImU32 kAccentDim = Rgb(120, 86, 26);

		enum class Phase
		{
			Loading,      // steps are being shown
			Completed,    // bar full, waiting for the editor's first frames
			FadingOut,
		};

		struct State
		{
			GLFWwindow* window = nullptr;
			ImGuiContext* imgui = nullptr;
			ImFont* font = nullptr;

			unsigned int logo = 0;
			float logo_ratio = 32.f / 28.f;

			std::string project_name;
			std::string project_path;
			std::string step = "Starting...";

			float progress = 0.f;   // last value given by SetStep
			float shown = 0.f;      // value drawn (animated in Complete)

			float scale = 1.f;
			bool visible = false;
			double opened_at = 0.0;

			Phase phase = Phase::Loading;
			int editor_frames = 0;
			double fade_start = 0.0;
		};

		State g;


		// The splash draws in the middle of the editor startup : whatever GL
		// context / ImGui context is current (HRL's, the editor's...) is
		// restored afterwards.
		struct ContextScope
		{
			GLFWwindow* previous_gl = glfwGetCurrentContext();
			ImGuiContext* previous_imgui = ImGui::GetCurrentContext();

			ContextScope()
			{
				glfwMakeContextCurrent(g.window);
				ImGui::SetCurrentContext(g.imgui);
			}

			~ContextScope()
			{
				ImGui::SetCurrentContext(previous_imgui);
				glfwMakeContextCurrent(previous_gl);
			}
		};


		void SleepMs(int ms)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(ms));
		}


		std::vector<unsigned char> ReadFile(const std::filesystem::path& path)
		{
			std::ifstream file(path, std::ios::binary);
			if (!file)
				return {};
			return std::vector<unsigned char>(
				std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		}


		// Lynxie (white pixel art) : a lynxie.png next to the editor replaces
		// the embedded one, like LynxieIcon. Needs the splash GL context.
		void CreateLogo()
		{
			std::vector<unsigned char> file =
				ReadFile(host::GetEditorDirectory() / "lynxie.png");

			const unsigned char* data = kLynxiePng;
			size_t size = sizeof(kLynxiePng);

			if (!file.empty())
			{
				data = file.data();
				size = file.size();
			}

			int width = 0;
			int height = 0;
			stbi_uc* pixels = stbi_load_from_memory(
				data, static_cast<int>(size), &width, &height, nullptr, 4);

			if (!pixels)
				return;

			glGenTextures(1, &g.logo);
			glBindTexture(GL_TEXTURE_2D, g.logo);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);   // sharp pixels
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
			glBindTexture(GL_TEXTURE_2D, 0);

			stbi_image_free(pixels);

			if (height > 0)
				g.logo_ratio = static_cast<float>(width) / static_cast<float>(height);
		}


		ImVec2 TextSize(float size, const char* text)
		{
			return g.font->CalcTextSizeA(size, FLT_MAX, 0.f, text);
		}


		// Long paths : "...end/of/the/path" so the project folder stays visible.
		std::string FitLeft(const std::string& text, float size, float max_width)
		{
			if (TextSize(size, text.c_str()).x <= max_width)
				return text;

			for (size_t start = 1; start < text.size(); ++start)
			{
				const std::string candidate = "..." + text.substr(start);
				if (TextSize(size, candidate.c_str()).x <= max_width)
					return candidate;
			}
			return "...";
		}


		std::string FitRight(const std::string& text, float size, float max_width)
		{
			if (TextSize(size, text.c_str()).x <= max_width)
				return text;

			for (size_t length = text.size(); length > 0; --length)
			{
				const std::string candidate = text.substr(0, length - 1) + "...";
				if (TextSize(size, candidate.c_str()).x <= max_width)
					return candidate;
			}
			return "...";
		}


		// Pixel spinner : 8 blocks on a circle, the bright one turns.
		void DrawSpinner(ImDrawList* draw, ImVec2 center, float radius, float block, double time)
		{
			const int head = static_cast<int>(time * 10.0) % 8;

			for (int i = 0; i < 8; ++i)
			{
				const float angle = static_cast<float>(i) * 3.14159265f / 4.f - 3.14159265f / 2.f;
				const ImVec2 p(
					std::floor(center.x + std::cos(angle) * radius - block * 0.5f),
					std::floor(center.y + std::sin(angle) * radius - block * 0.5f));

				const int distance = (head - i + 8) % 8;
				const int alpha = 255 - distance * 28;

				draw->AddRectFilled(p, ImVec2(p.x + block, p.y + block),
				                    distance == 0 ? kAccentHot : Rgb(217, 154, 43, std::max(alpha, 40)));
			}
		}


		void DrawProgressBar(ImDrawList* draw, ImVec2 min, ImVec2 max, float value, double time)
		{
			const float s = g.scale;
			const float px = std::max(1.f, std::floor(s));

			// Frame : black outline, inset track.
			draw->AddRectFilled(min, max, kOutline);
			const ImVec2 in_min(min.x + px, min.y + px);
			const ImVec2 in_max(max.x - px, max.y - px);
			draw->AddRectFilled(in_min, in_max, kDeep);

			const float fill_x = std::floor(in_min.x + (in_max.x - in_min.x) * std::clamp(value, 0.f, 1.f));

			if (fill_x <= in_min.x)
				return;

			draw->AddRectFilled(in_min, ImVec2(fill_x, in_max.y), kAccent);

			// Top highlight / bottom shade : pixel bevel.
			draw->AddRectFilled(in_min, ImVec2(fill_x, in_min.y + px), kAccentHot);
			draw->AddRectFilled(ImVec2(in_min.x, in_max.y - px), ImVec2(fill_x, in_max.y), kAccentDim);

			// Segments.
			const float segment = std::floor(10.f * s);
			for (float x = in_min.x + segment; x < fill_x; x += segment)
				draw->AddRectFilled(ImVec2(x, in_min.y), ImVec2(x + px, in_max.y), kAccentDim);

			// Moving shine inside the filled part.
			draw->PushClipRect(in_min, ImVec2(fill_x, in_max.y), true);
			const float span = (in_max.x - in_min.x) * 1.4f;
			const float shine_x = in_min.x - 0.2f * (in_max.x - in_min.x) +
			                      static_cast<float>(std::fmod(time * 0.6, 1.0)) * span;
			const float shine_w = std::floor(36.f * s);
			draw->AddRectFilled(ImVec2(std::floor(shine_x), in_min.y),
			                    ImVec2(std::floor(shine_x + shine_w), in_max.y),
			                    Rgb(255, 230, 170, 70));
			draw->PopClipRect();
		}


		void Draw()
		{
			ImDrawList* draw = ImGui::GetBackgroundDrawList();
			const ImVec2 size = ImGui::GetIO().DisplaySize;
			const float s = g.scale;
			const float px = std::max(1.f, std::floor(s));
			const double time = glfwGetTime();

			// Window : outline, bevel, background, accent stripe on top.
			draw->AddRectFilled(ImVec2(0.f, 0.f), size, kOutline);
			draw->AddRectFilled(ImVec2(px, px), ImVec2(size.x - px, size.y - px), kBevel);
			draw->AddRectFilled(ImVec2(px * 2.f, px * 2.f), ImVec2(size.x - px, size.y - px), kBackground);
			draw->AddRectFilled(ImVec2(px, px), ImVec2(size.x - px, px + std::floor(4.f * s)), kAccent);

			const float pad = std::floor(32.f * s);
			const float content_w = size.x - pad * 2.f;

			// Lynxie + title.
			const float logo_h = std::floor(112.f * s);
			const float logo_w = std::floor(logo_h * g.logo_ratio);
			const ImVec2 logo_min(pad, pad + std::floor(6.f * s));

			if (g.logo)
			{
				draw->AddImage(
					static_cast<ImTextureID>(static_cast<uintptr_t>(g.logo)),
					logo_min, ImVec2(logo_min.x + logo_w, logo_min.y + logo_h),
					ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), kText);
			}

			const float title_x = pad + (g.logo ? logo_w + std::floor(24.f * s) : 0.f);
			const float title_size = std::floor(72.f * s);
			draw->AddText(g.font, title_size, ImVec2(title_x, pad), kText, "LYNX");

			const float sub_size = std::floor(18.f * s);
			const float sub_y = pad + title_size + std::floor(4.f * s);
			draw->AddText(g.font, sub_size, ImVec2(title_x, sub_y), kTextDim, "ENGINE / EDITOR");

			const float title_w = TextSize(title_size, "LYNX").x;
			draw->AddRectFilled(
				ImVec2(title_x, sub_y + sub_size + std::floor(8.f * s)),
				ImVec2(title_x + title_w, sub_y + sub_size + std::floor(8.f * s) + px * 2.f),
				kAccent);

			// Busy indicator, top right.
			DrawSpinner(draw,
			            ImVec2(size.x - pad - std::floor(14.f * s), pad + std::floor(14.f * s)),
			            std::floor(12.f * s), std::floor(5.f * s), time);

			// Project.
			float y = logo_min.y + logo_h + std::floor(30.f * s);

			const float label_size = std::floor(15.f * s);
			draw->AddText(g.font, label_size, ImVec2(pad, y), kAccent, "OPENING PROJECT");
			y += label_size + std::floor(6.f * s);

			const float name_size = std::floor(30.f * s);
			const std::string name = FitRight(g.project_name, name_size, content_w);
			draw->AddText(g.font, name_size, ImVec2(pad, y), kText, name.c_str());
			y += name_size + std::floor(4.f * s);

			const float path_size = std::floor(15.f * s);
			const std::string path = FitLeft(g.project_path, path_size, content_w);
			draw->AddText(g.font, path_size, ImVec2(pad, y), kTextDim, path.c_str());

			// Step + progress bar, at the bottom.
			const float bar_h = std::floor(14.f * s);
			const float bar_y = size.y - pad - bar_h;

			char percent[16];
			std::snprintf(percent, sizeof(percent), "%d%%",
			              static_cast<int>(std::round(std::clamp(g.shown, 0.f, 1.f) * 100.f)));

			const float step_size = std::floor(18.f * s);
			const float step_y = bar_y - std::floor(8.f * s) - step_size;
			const float percent_w = TextSize(step_size, percent).x;

			const std::string step =
				FitRight(g.step, step_size, content_w - percent_w - std::floor(16.f * s));

			draw->AddText(g.font, step_size, ImVec2(pad, step_y), kText, step.c_str());
			draw->AddText(g.font, step_size, ImVec2(size.x - pad - percent_w, step_y), kTextDim, percent);

			DrawProgressBar(draw, ImVec2(pad, bar_y), ImVec2(size.x - pad, bar_y + bar_h), g.shown, time);
		}


		void RenderFrame()
		{
			if (!g.window)
				return;

			// A splash cannot be closed : Alt+F4 / close requests are ignored.
			glfwSetWindowShouldClose(g.window, GLFW_FALSE);

			ContextScope scope;

			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			Draw();

			ImGui::Render();

			int width = 0;
			int height = 0;
			glfwGetFramebufferSize(g.window, &width, &height);
			glViewport(0, 0, width, height);
			glClearColor(37.f / 255.f, 37.f / 255.f, 37.f / 255.f, 1.f);
			glClear(GL_COLOR_BUFFER_BIT);

			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

			glfwSwapBuffers(g.window);
		}


		// Keeps the application responsive (no "Not responding") and redraws.
		void Pump()
		{
			glfwPollEvents();
			RenderFrame();
		}


		float EaseOut(float t)
		{
			t = std::clamp(t, 0.f, 1.f);
			return 1.f - (1.f - t) * (1.f - t) * (1.f - t);
		}
	}


	void Open(const host::GameProject& project)
	{
		g.project_name = project.name.empty() ? project.root.filename().string() : project.name;
		g.project_path = project.root.string();

		if (g.window)
		{
			Show();
			return;
		}

		if (!glfwInit())
		{
			std::cerr << "[SPLASH] glfwInit failed\n";
			return;
		}

		// Size from the monitor DPI, centered in its work area (no taskbar).
		GLFWmonitor* monitor = glfwGetPrimaryMonitor();

		float scale_x = 1.f;
		float scale_y = 1.f;
		if (monitor)
			glfwGetMonitorContentScale(monitor, &scale_x, &scale_y);

		g.scale = std::clamp(std::round(scale_x * 4.f) / 4.f, 1.f, 3.f);

		const int width = static_cast<int>(kWidth * g.scale);
		const int height = static_cast<int>(kHeight * g.scale);

		int area_x = 0;
		int area_y = 0;
		int area_w = width;
		int area_h = height;
		if (monitor)
			glfwGetMonitorWorkarea(monitor, &area_x, &area_y, &area_w, &area_h);

		const int x = area_x + (area_w - width) / 2;
		const int y = area_y + (area_h - height) / 2;

		glfwDefaultWindowHints();
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);      // shown once a frame is drawn
		glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
		glfwWindowHint(GLFW_POSITION_X, x);
		glfwWindowHint(GLFW_POSITION_Y, y);

		const std::string title = "Lynx - " + g.project_name;
		g.window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);

		glfwDefaultWindowHints();

		if (!g.window)
		{
			std::cerr << "[SPLASH] Could not create the splash window\n";
			return;
		}

		lynx::editor::window_icon::Apply(g.window);
		glfwSetWindowPos(g.window, x, y);

		// GL + ImGui contexts of the splash.
		GLFWwindow* previous_gl = glfwGetCurrentContext();
		ImGuiContext* previous_imgui = ImGui::GetCurrentContext();

		glfwMakeContextCurrent(g.window);
		glfwSwapInterval(0);   // a step must never wait for the vsync

		IMGUI_CHECKVERSION();
		g.imgui = ImGui::CreateContext();
		ImGui::SetCurrentContext(g.imgui);

		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.LogFilename = nullptr;
		io.ConfigFlags |= ImGuiConfigFlags_NoMouse |
		                  ImGuiConfigFlags_NoKeyboard;

		// Same font as the editor (the editor loads its own afterwards).
		fonts::Load(20.f * g.scale);
		g.font = fonts::Editor();   // never nullptr after Load (ImGui's font at worst)

		ImGui::StyleColorsDark();

		ImGui_ImplGlfw_InitForOpenGL(g.window, false);   // no input on a splash
		ImGui_ImplOpenGL3_Init("#version 330");

		CreateLogo();

		ImGui::SetCurrentContext(previous_imgui);
		glfwMakeContextCurrent(previous_gl);

		g.opened_at = glfwGetTime();
		g.phase = Phase::Loading;
		g.progress = 0.f;
		g.shown = 0.f;
		g.step = "Opening the project...";
		g.visible = false;

		std::cout << "[SPLASH] Loading " << g.project_name << "\n";

		Show();
	}


	void SetStep(const std::string& text, float progress)
	{
		if (!g.window)
			return;

		std::cout << "[LOADING] " << text << "\n";

		g.step = text;
		g.progress = std::max(g.progress, std::clamp(progress, 0.f, 1.f));
		g.shown = g.progress;

		if (g.visible)
			Pump();
	}


	void Hide()
	{
		if (!g.window || !g.visible)
			return;

		glfwHideWindow(g.window);
		g.visible = false;
	}


	void Show()
	{
		if (!g.window || g.visible)
			return;

		// First frame drawn before the window appears : never an empty window.
		glfwSetWindowOpacity(g.window, 0.f);
		RenderFrame();

		glfwShowWindow(g.window);
		g.visible = true;

		const double start = glfwGetTime();

		for (;;)
		{
			const double t = (glfwGetTime() - start) / kFadeInSeconds;
			if (t >= 1.0)
				break;

			glfwSetWindowOpacity(g.window, static_cast<float>(t));
			Pump();
			SleepMs(8);
		}

		glfwSetWindowOpacity(g.window, 1.f);
		Pump();
	}


	void Complete()
	{
		if (!g.window)
			return;

		Show();

		g.step = "Opening the editor...";

		// The bar fills up to 100 %, and the splash stays at least
		// kMinVisibleSeconds on the screen.
		const double now = glfwGetTime();
		const double remaining = kMinVisibleSeconds - (now - g.opened_at);
		const double duration = std::max(kFillSeconds, remaining);
		const float from = g.shown;

		for (;;)
		{
			const double t = (glfwGetTime() - now) / duration;
			if (t >= 1.0)
				break;

			g.shown = from + (1.f - from) * EaseOut(static_cast<float>(t));
			Pump();
			SleepMs(15);
		}

		g.progress = 1.f;
		g.shown = 1.f;
		Pump();

		// The editor window is shown next : the splash stays above it until
		// it fades out.
		glfwSetWindowAttrib(g.window, GLFW_FLOATING, GLFW_TRUE);

		g.phase = Phase::Completed;
		g.editor_frames = 0;
	}


	void Tick()
	{
		if (!g.window || g.phase == Phase::Loading)
			return;

		if (g.phase == Phase::Completed)
		{
			// The first editor frames (dock layout, fonts...) stay hidden.
			if (++g.editor_frames < kEditorFramesBeforeFade)
				return;

			g.phase = Phase::FadingOut;
			g.fade_start = glfwGetTime();
		}

		const double t = (glfwGetTime() - g.fade_start) / kFadeOutSeconds;

		if (t >= 1.0)
		{
			Close();
			return;
		}

		glfwSetWindowOpacity(g.window, 1.f - EaseOut(static_cast<float>(t)));
		RenderFrame();
	}


	void Close()
	{
		if (!g.window)
			return;

		{
			ContextScope scope;

			if (g.logo)
				glDeleteTextures(1, &g.logo);

			ImGui_ImplOpenGL3_Shutdown();
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext(g.imgui);
		}

		glfwDestroyWindow(g.window);

		g = State{};
	}


	bool IsOpen()
	{
		return g.window != nullptr;
	}
}
