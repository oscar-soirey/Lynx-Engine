#include "TitleBar.h"

#include "LynxieIcon.h"

#include <imgui/imgui_internal.h>   // BeginViewportSideBar

#include <glfw/glfw3.h>

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef _WIN32
// Before any <windows.h> (glfw3native.h includes it) : no min / max macros.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <glfw/glfw3native.h>
#endif

namespace lynx::editor::title_bar
{
	namespace
	{
		GLFWwindow* g_window = nullptr;
		bool g_active = false;

		// Geometry of the last frame, in client pixels (= ImGui coordinates) :
		// read by the hit test of Windows, between two frames.
		float g_height = 0.f;
		std::vector<ImRect> g_interactive;      // being filled this frame
		std::vector<ImRect> g_interactive_last; // the one the hit test uses

		bool IsMaximized()
		{
			return g_window && glfwGetWindowAttrib(g_window, GLFW_MAXIMIZED) == GLFW_TRUE;
		}

#ifdef _WIN32
		WNDPROC g_previous_proc = nullptr;

		int DpiOf(HWND hwnd)
		{
			// GetDpiForWindow : Windows 10 1607+ (looked up : older SDK headers / systems).
			using Fn = UINT(WINAPI*)(HWND);
			static Fn fn = reinterpret_cast<Fn>(
				reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
			if (fn)
				return static_cast<int>(fn(hwnd));
			HDC dc = GetDC(hwnd);
			const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
			ReleaseDC(hwnd, dc);
			return dpi > 0 ? dpi : 96;
		}

		int FrameThickness(HWND hwnd, bool vertical)
		{
			// Thickness of the (invisible) frame of a maximized window.
			const int frame = GetSystemMetrics(vertical ? SM_CYFRAME : SM_CXFRAME);
			const int padded = GetSystemMetrics(SM_CXPADDEDBORDER);
			(void)hwnd;
			return frame + padded;
		}

		LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
		{
			switch (message)
			{
			case WM_NCCALCSIZE:
				if (wparam == TRUE)
				{
					// The whole window is client area (no frame, no system title bar).
					// Maximized, Windows puts the frame outside of the screen : keep the
					// client area on the screen.
					if (IsZoomed(hwnd))
					{
						auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
						const int fx = FrameThickness(hwnd, false);
						const int fy = FrameThickness(hwnd, true);
						params->rgrc[0].left += fx;
						params->rgrc[0].right -= fx;
						params->rgrc[0].top += fy;
						params->rgrc[0].bottom -= fy;
					}
					return 0;
				}
				break;

			case WM_NCACTIVATE:
				// No repaint of the (removed) native frame when the focus changes.
				return DefWindowProcW(hwnd, message, wparam, -1);

			case WM_NCHITTEST:
			{
				POINT point{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
				ScreenToClient(hwnd, &point);
				RECT client;
				GetClientRect(hwnd, &client);

				// Edges and corners : resize.
				if (!IsZoomed(hwnd))
				{
					const int border = std::max(4, MulDiv(6, DpiOf(hwnd), 96));
					const bool left = point.x < border;
					const bool right = point.x >= client.right - border;
					const bool top = point.y < border;
					const bool bottom = point.y >= client.bottom - border;
					if (top && left) return HTTOPLEFT;
					if (top && right) return HTTOPRIGHT;
					if (bottom && left) return HTBOTTOMLEFT;
					if (bottom && right) return HTBOTTOMRIGHT;
					if (left) return HTLEFT;
					if (right) return HTRIGHT;
					if (top) return HTTOP;
					if (bottom) return HTBOTTOM;
				}

				// The bar : moves the window, except over its buttons.
				if (point.y >= 0 && point.y < static_cast<LONG>(g_height))
				{
					const ImVec2 p(static_cast<float>(point.x), static_cast<float>(point.y));
					for (const ImRect& r : g_interactive_last)
						if (r.Contains(p))
							return HTCLIENT;
					return HTCAPTION;
				}
				return HTCLIENT;
			}

			default:
				break;
			}
			return CallWindowProcW(g_previous_proc, hwnd, message, wparam, lparam);
		}

		void ExtendShadow(HWND hwnd)
		{
			// The DWM shadow of a window without frame (dwmapi.dll looked up : no link change).
			struct Margins { int left, right, top, bottom; };
			using Fn = HRESULT(WINAPI*)(HWND, const Margins*);
			HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
			if (!dwm)
				return;
			auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(dwm, "DwmExtendFrameIntoClientArea")));
			if (fn)
			{
				const Margins margins{ 0, 0, 1, 0 };
				fn(hwnd, &margins);
			}
		}
#endif

		// ---- Drawing --------------------------------------------------------

		enum class Glyph { Minimize, Maximize, Restore, Close };

		bool CaptionButton(const char* id, Glyph glyph, float width, float height)
		{
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, height));
			const bool hovered = ImGui::IsItemHovered();
			const bool held = ImGui::IsItemActive();
			AddInteractive(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			ImDrawList* dl = ImGui::GetWindowDrawList();
			const bool close = glyph == Glyph::Close;
			if (hovered || held)
			{
				const ImU32 bg = close ? (held ? IM_COL32(170, 30, 30, 255) : IM_COL32(210, 45, 45, 255))
				                       : ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered);
				dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), bg);
			}

			const ImU32 color = close && (hovered || held) ? IM_COL32(255, 255, 255, 255) : ImGui::GetColorU32(ImGuiCol_Text);
			const float s = std::floor(height * 0.30f);           // glyph size
			const float cx = std::floor(pos.x + width * 0.5f);
			const float cy = std::floor(pos.y + height * 0.5f);
			const float t = std::max(1.f, std::floor(height / 22.f));   // line thickness (pixel look)
			switch (glyph)
			{
			case Glyph::Minimize:
				dl->AddRectFilled(ImVec2(cx - s * 0.5f, cy), ImVec2(cx + s * 0.5f, cy + t), color);
				break;
			case Glyph::Maximize:
				dl->AddRect(ImVec2(cx - s * 0.5f, cy - s * 0.5f), ImVec2(cx + s * 0.5f, cy + s * 0.5f), color, 0.f, 0, t);
				break;
			case Glyph::Restore:
			{
				const float o = std::floor(s * 0.25f);
				dl->AddRect(ImVec2(cx - s * 0.5f, cy - s * 0.5f + o), ImVec2(cx + s * 0.5f - o, cy + s * 0.5f), color, 0.f, 0, t);
				dl->AddLine(ImVec2(cx - s * 0.5f + o, cy - s * 0.5f), ImVec2(cx + s * 0.5f, cy - s * 0.5f), color, t);
				dl->AddLine(ImVec2(cx + s * 0.5f, cy - s * 0.5f), ImVec2(cx + s * 0.5f, cy + s * 0.5f - o), color, t);
				break;
			}
			case Glyph::Close:
				dl->AddLine(ImVec2(cx - s * 0.5f, cy - s * 0.5f), ImVec2(cx + s * 0.5f, cy + s * 0.5f), color, t + 0.5f);
				dl->AddLine(ImVec2(cx + s * 0.5f, cy - s * 0.5f), ImVec2(cx - s * 0.5f, cy + s * 0.5f), color, t + 0.5f);
				break;
			}
			return pressed;
		}
	}


	bool Install(GLFWwindow* window)
	{
		g_window = window;
#ifdef _WIN32
		HWND hwnd = window ? glfwGetWin32Window(window) : nullptr;
		if (!hwnd)
			return false;
		g_previous_proc = reinterpret_cast<WNDPROC>(
			SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc)));
		if (!g_previous_proc)
			return false;
		ExtendShadow(hwnd);
		// Recompute the frame now (WM_NCCALCSIZE).
		SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		g_active = true;
		return true;
#else
		g_active = false;
		return false;
#endif
	}

	bool Active()
	{
		return g_active;
	}

	float Height()
	{
		return g_height;
	}

	void ForceDraw(bool force)
	{
		g_active = force;
	}

	int HitTestBar(float x, float y)
	{
		if (y < 0.f || y >= g_height)
			return 0;
		for (const ImRect& r : g_interactive_last)
			if (r.Contains(ImVec2(x, y)))
				return 0;
		return 1;
	}

	void AddInteractive(const ImVec2& min, const ImVec2& max)
	{
		g_interactive.push_back(ImRect(min, max));
	}

	void Draw(const std::string& title, const std::function<void()>& on_close, const std::function<void()>& on_lynxie)
	{
		if (!g_active)
			return;

		g_interactive.clear();

		ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float height = std::floor(ImGui::GetFontSize() * 1.9f);
		const bool maximized = IsMaximized();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
		const bool open = ImGui::BeginViewportSideBar("##TitleBar", viewport, ImGuiDir_Up, height,
		                                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
		                                              ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(3);

		if (open)
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 origin = ImGui::GetWindowPos();
			const float width = ImGui::GetWindowWidth();
			const bool focused = !g_window || glfwGetWindowAttrib(g_window, GLFW_FOCUSED) == GLFW_TRUE;
			const ImU32 text = ImGui::GetColorU32(focused ? ImGuiCol_Text : ImGuiCol_TextDisabled);

			// Lynxie on the left : a button (opens her chat)
			{
				const float icon_h = std::floor(height * 0.74f);
				const float icon_w = lynxie_icon::WidthFor(icon_h);
				const ImVec2 at(origin.x + std::floor(height * 0.35f), origin.y + std::floor((height - icon_h) * 0.5f));
				ImGui::SetCursorScreenPos(ImVec2(at.x - 4.f, origin.y));
				const bool clicked = ImGui::InvisibleButton("##TitleLynxie", ImVec2(icon_w + 8.f, height));
				const bool hovered = ImGui::IsItemHovered();
				AddInteractive(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				if (hovered)
					dl->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_ButtonHovered));
				if (lynxie_icon::Ready())
					lynxie_icon::DrawAt(dl, ImVec2(at.x, at.y + (hovered ? -1.f : 0.f)), icon_h, text);
				if (hovered)
					ImGui::SetTooltip("Lynxie : ask a question about the engine or the level");
				if (clicked && on_lynxie)
					on_lynxie();
			}

			// Title : a dark tab hanging from the top, in the middle (drag area too)
			{
				const ImVec2 text_size = ImGui::CalcTextSize(title.c_str());
				const float tab_h = std::floor(height * 0.78f);
				const float slant = std::floor(tab_h * 0.38f);
				const float pad = std::floor(height * 0.9f);
				const float top_w = text_size.x + pad * 2.f + slant * 2.f;
				const float cx = std::floor(origin.x + width * 0.5f);
				const float x0 = std::floor(cx - top_w * 0.5f);
				const float x1 = x0 + top_w;
				const float y0 = origin.y;
				const float y1 = origin.y + tab_h;
				const ImU32 tab = focused ? IM_COL32(26, 24, 30, 255) : IM_COL32(70, 66, 74, 255);
				// Slanted sides, bottom corners slightly rounded.
				const float r = std::floor(tab_h * 0.28f);
				const ImVec2 tl(x0, y0), tr(x1, y0), br(x1 - slant, y1), bl(x0 + slant, y1);
				auto toward = [](const ImVec2& from, const ImVec2& to, float d)
				{
					const float dx = to.x - from.x, dy = to.y - from.y;
					const float len = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
					return ImVec2(from.x + dx / len * d, from.y + dy / len * d);
				};
				auto outline = [&]()
				{
					dl->PathLineTo(tl);
					dl->PathLineTo(tr);
					dl->PathLineTo(toward(br, tr, r));
					dl->PathBezierQuadraticCurveTo(br, toward(br, bl, r), 8);
					dl->PathLineTo(toward(bl, br, r));
					dl->PathBezierQuadraticCurveTo(bl, toward(bl, tl, r), 8);
				};
				outline();
				dl->PathFillConvex(tab);
				// light edge on the sides and the bottom (pixel look)
				outline();
				dl->PathStroke(IM_COL32(255, 255, 255, 40), ImDrawFlags_Closed, 1.f);
				const ImU32 cream = focused ? IM_COL32(244, 229, 172, 255) : IM_COL32(200, 192, 160, 255);
				dl->AddText(ImVec2(std::floor(cx - text_size.x * 0.5f), std::floor(y0 + (tab_h - text_size.y) * 0.5f)), cream, title.c_str());
			}

			// Buttons on the right (Windows layout)
			const float button_w = std::floor(height * 1.55f);
			ImGui::SetCursorScreenPos(ImVec2(origin.x + width - button_w * 3.f, origin.y));
			if (CaptionButton("##TitleMinimize", Glyph::Minimize, button_w, height) && g_window)
				glfwIconifyWindow(g_window);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Minimize");
			ImGui::SameLine();
			if (CaptionButton("##TitleMaximize", maximized ? Glyph::Restore : Glyph::Maximize, button_w, height) && g_window)
			{
				if (maximized)
					glfwRestoreWindow(g_window);
				else
					glfwMaximizeWindow(g_window);
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(maximized ? "Restore" : "Maximize");
			ImGui::SameLine();
			if (CaptionButton("##TitleClose", Glyph::Close, button_w, height) && on_close)
				on_close();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Close");

			// Bottom line of the bar
			dl->AddLine(ImVec2(origin.x, origin.y + height - 1.f), ImVec2(origin.x + width, origin.y + height - 1.f),
			            ImGui::GetColorU32(ImGuiCol_Border));
		}
		ImGui::End();

		// Outline of the window (no native frame any more), not when maximized.
		if (!maximized)
		{
			const bool focused = !g_window || glfwGetWindowAttrib(g_window, GLFW_FOCUSED) == GLFW_TRUE;
			ImGui::GetForegroundDrawList(viewport)->AddRect(
				viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
				ImGui::GetColorU32(focused ? ImGuiCol_SeparatorActive : ImGuiCol_Border));
		}

		g_height = height;
		g_interactive_last = g_interactive;
	}
}
