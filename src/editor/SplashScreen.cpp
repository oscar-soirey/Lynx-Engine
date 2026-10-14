#include "SplashScreen.h"

#include "raven/RavenUi.h"

#include <Raven.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <thread>

// =============================================================================
// Splash screen in Raven : a borderless window whose only widget is a Custom
// painted by Draw(). Everything runs on the main (UI) thread ; every step
// pumps Raven (Rpoll_events + Rdraw_frame) because the caller is about to block.
// Raven must be initialized (Rinit + ui::Init) before Open().
// =============================================================================

namespace lynx::editor::splash
{
	namespace
	{
		constexpr int kWidth = 640;
		constexpr int kHeight = 360;

		constexpr double kMinVisibleSeconds = 1.0;   // no flash when loading is instant
		constexpr double kFadeInSeconds = 0.15;
		constexpr double kFillSeconds = 0.25;        // bar -> 100 % at the end, at least
		constexpr double kFadeOutSeconds = 0.25;
		constexpr int kEditorFramesBeforeFade = 2;   // the editor's first frames stay hidden

		struct Color { float r, g, b, a; };

		constexpr Color Rgb(int r, int g, int b, int a = 255)
		{
			return { r / 255.f, g / 255.f, b / 255.f, a / 255.f };
		}

		constexpr Color kOutline = Rgb(15, 15, 15);
		constexpr Color kDeep = Rgb(30, 30, 30);
		constexpr Color kBackground = Rgb(37, 37, 37);
		constexpr Color kBevel = Rgb(61, 61, 61);
		constexpr Color kText = Rgb(230, 230, 230);
		constexpr Color kTextDim = Rgb(128, 128, 128);
		constexpr Color kAccent = Rgb(217, 154, 43);
		constexpr Color kAccentHot = Rgb(240, 185, 74);
		constexpr Color kAccentDim = Rgb(120, 86, 26);

		enum class Phase
		{
			Loading,      // steps are being shown
			Completed,    // bar full, waiting for the editor's first frames
			FadingOut,
		};

		struct State
		{
			Rid window = R_INVALID_ID;
			Rid canvas = R_INVALID_ID;
			Rid logo = R_INVALID_ID;
			bool own_logo = false;
			float logo_ratio = 32.f / 28.f;

			std::string project_name;
			std::string project_path;
			std::string step = "Starting...";

			float progress = 0.f;   // last value given by SetStep
			float shown = 0.f;      // value drawn (animated in Complete)

			bool visible = false;
			double opened_at = 0.0;

			Phase phase = Phase::Loading;
			int editor_frames = 0;
			double fade_start = 0.0;
		};

		State g;

		double Now()
		{
			using namespace std::chrono;
			static const steady_clock::time_point start = steady_clock::now();
			return duration<double>(steady_clock::now() - start).count();
		}

		void SleepMs(int ms)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(ms));
		}

		float EaseOut(float t)
		{
			t = std::clamp(t, 0.f, 1.f);
			return 1.f - (1.f - t) * (1.f - t) * (1.f - t);
		}

		// ---- drawing -------------------------------------------------------------

		void Rect(RPainter* p, float x, float y, float w, float h, Color c)
		{
			if (w > 0.f && h > 0.f)
				Rdraw_rect(p, x, y, w, h, 0.f, c.r, c.g, c.b, c.a);
		}

		void Text(RPainter* p, float size, float x, float y, const std::string& text, Color c)
		{
			Rdraw_text(p, R_INVALID_ID, size, x, y, text.c_str(), c.r, c.g, c.b, c.a);
		}

		float TextWidth(float size, const std::string& text)
		{
			float w = 0.f, h = 0.f;
			Rfont_measure_text(R_INVALID_ID, size, text.c_str(), &w, &h);
			if (w <= 0.f)   // no font yet : a rough estimate (monospace pixel font)
				w = size * 0.6f * static_cast<float>(text.size());
			return w;
		}

		// Long paths : "...end/of/the/path" so the project folder stays visible.
		std::string FitLeft(const std::string& text, float size, float max_width)
		{
			if (TextWidth(size, text) <= max_width)
				return text;
			for (size_t start = 1; start < text.size(); ++start)
			{
				const std::string candidate = "..." + text.substr(start);
				if (TextWidth(size, candidate) <= max_width)
					return candidate;
			}
			return "...";
		}

		std::string FitRight(const std::string& text, float size, float max_width)
		{
			if (TextWidth(size, text) <= max_width)
				return text;
			for (size_t length = text.size(); length > 0; --length)
			{
				const std::string candidate = text.substr(0, length - 1) + "...";
				if (TextWidth(size, candidate) <= max_width)
					return candidate;
			}
			return "...";
		}

		// Pixel spinner : 8 blocks on a circle, the bright one turns.
		void DrawSpinner(RPainter* p, float cx, float cy, float radius, float block, double time)
		{
			const int head = static_cast<int>(time * 10.0) % 8;
			for (int i = 0; i < 8; ++i)
			{
				const float angle = static_cast<float>(i) * 3.14159265f / 4.f - 3.14159265f / 2.f;
				const float x = std::floor(cx + std::cos(angle) * radius - block * 0.5f);
				const float y = std::floor(cy + std::sin(angle) * radius - block * 0.5f);
				const int distance = (head - i + 8) % 8;
				Color c = kAccent;
				c.a = std::max(255 - distance * 28, 40) / 255.f;
				Rect(p, x, y, block, block, distance == 0 ? kAccentHot : c);
			}
		}

		void DrawProgressBar(RPainter* p, float x, float y, float w, float h, float value, double time)
		{
			const float px = 1.f;
			Rect(p, x, y, w, h, kOutline);
			const float ix = x + px, iy = y + px, iw = w - 2 * px, ih = h - 2 * px;
			Rect(p, ix, iy, iw, ih, kDeep);

			const float fill = std::floor(iw * std::clamp(value, 0.f, 1.f));
			if (fill <= 0.f)
				return;

			Rect(p, ix, iy, fill, ih, kAccent);
			Rect(p, ix, iy, fill, px, kAccentHot);                // pixel bevel
			Rect(p, ix, iy + ih - px, fill, px, kAccentDim);

			for (float sx = ix + 10.f; sx < ix + fill; sx += 10.f)   // segments
				Rect(p, sx, iy, px, ih, kAccentDim);

			// Moving shine inside the filled part.
			Rdraw_push_clip_rect(p, ix, iy, fill, ih);
			const float span = iw * 1.4f;
			const float shine_x = ix - 0.2f * iw + static_cast<float>(std::fmod(time * 0.6, 1.0)) * span;
			Rect(p, std::floor(shine_x), iy, 36.f, ih, Rgb(255, 230, 170, 70));
			Rdraw_pop_clip_rect(p);
		}

		void Draw(Rid, RPainter* p, float width, float height, void*)
		{
			const double time = Now();
			const float px = 1.f;

			// Window : outline, bevel, background, accent stripe on top.
			Rect(p, 0, 0, width, height, kOutline);
			Rect(p, px, px, width - 2 * px, height - 2 * px, kBevel);
			Rect(p, 2 * px, 2 * px, width - 3 * px, height - 3 * px, kBackground);
			Rect(p, px, px, width - 2 * px, 4.f, kAccent);

			const float pad = 32.f;
			const float content_w = width - pad * 2.f;

			// Lynxie + title.
			const float logo_h = 112.f;
			const float logo_w = std::floor(logo_h * g.logo_ratio);
			const float logo_y = pad + 6.f;
			if (g.logo != R_INVALID_ID)
				Rdraw_image(p, g.logo, pad, logo_y, logo_w, logo_h, kText.r, kText.g, kText.b, 1.f);

			const float title_x = pad + (g.logo != R_INVALID_ID ? logo_w + 24.f : 0.f);
			const float title_size = 72.f;
			Text(p, title_size, title_x, pad, "LYNX", kText);

			const float sub_size = 18.f;
			const float sub_y = pad + title_size + 4.f;
			Text(p, sub_size, title_x, sub_y, "ENGINE / EDITOR", kTextDim);
			Rect(p, title_x, sub_y + sub_size + 8.f, TextWidth(title_size, "LYNX"), 2 * px, kAccent);

			// Busy indicator, top right.
			DrawSpinner(p, width - pad - 14.f, pad + 14.f, 12.f, 5.f, time);

			// Project.
			float y = logo_y + logo_h + 30.f;
			Text(p, 15.f, pad, y, "OPENING PROJECT", kAccent);
			y += 21.f;
			Text(p, 30.f, pad, y, FitRight(g.project_name, 30.f, content_w), kText);
			y += 34.f;
			Text(p, 15.f, pad, y, FitLeft(g.project_path, 15.f, content_w), kTextDim);

			// Step + progress bar, at the bottom.
			const float bar_h = 14.f;
			const float bar_y = height - pad - bar_h;

			char percent[16];
			std::snprintf(percent, sizeof(percent), "%d%%",
			              static_cast<int>(std::round(std::clamp(g.shown, 0.f, 1.f) * 100.f)));

			const float step_size = 18.f;
			const float step_y = bar_y - 8.f - step_size;
			const float percent_w = TextWidth(step_size, percent);

			Text(p, step_size, pad, step_y, FitRight(g.step, step_size, content_w - percent_w - 16.f), kText);
			Text(p, step_size, width - pad - percent_w, step_y, percent, kTextDim);

			DrawProgressBar(p, pad, bar_y, content_w, bar_h, g.shown, time);
		}

		// A splash cannot be closed : Alt+F4 / close requests are refused.
		int RefuseClose(Rid, void*) { return R_FALSE; }

		// Lynxie (white pixel art) : a lynxie.png next to the editor replaces
		// the embedded one.
		void CreateLogo()
		{
			const std::filesystem::path custom = host::GetEditorDirectory() / "lynxie.png";
			std::error_code error;
			if (std::filesystem::is_regular_file(custom, error))
			{
				g.logo = Rtexture_create_from_file(custom.string().c_str());
				g.own_logo = g.logo != R_INVALID_ID;
			}
			if (g.logo == R_INVALID_ID)
				g.logo = ui::Texture("icons/lynxie.png");
			if (g.logo == R_INVALID_ID)
				return;

			Rtexture_set_filter(g.logo, R_FILTER_NEAREST);   // sharp pixels
			int w = 0, h = 0;
			Rtexture_get_size(g.logo, &w, &h);
			if (w > 0 && h > 0)
				g.logo_ratio = static_cast<float>(w) / static_cast<float>(h);
		}

		// Keeps the application responsive (no "Not responding") and redraws.
		void Pump()
		{
			if (g.window == R_INVALID_ID)
				return;
			Rwidget_invalidate(g.canvas);
			Rpoll_events();
			Rdraw_frame();
		}
	}


	void Open(const host::GameProject& project)
	{
		g.project_name = project.name.empty() ? project.root.filename().string() : project.name;
		g.project_path = project.root.string();

		if (g.window != R_INVALID_ID)
		{
			Show();
			return;
		}

		g.window = Rwindow_create(("Lynx - " + g.project_name).c_str(), kWidth, kHeight,
		                          R_WINDOW_BORDERLESS | R_WINDOW_NO_RESIZE | R_WINDOW_HIDDEN);
		if (g.window == R_INVALID_ID)
		{
			std::cerr << "[SPLASH] Could not create the splash window\n";
			return;
		}

		Rwindow_set_close_callback(g.window, RefuseClose, nullptr);
		Rwindow_set_clear_color(g.window, kBackground.r, kBackground.g, kBackground.b, 1.f);
		Rwindow_center(g.window);

		g.canvas = Rwidget_create(R_WIDGET_CUSTOM);
		Rcustom_set_paint_callback(g.canvas, Draw, nullptr);
		Rwindow_set_content(g.window, g.canvas);
		Rwindow_set_continuous_redraw(g.window, R_TRUE);   // spinner, shine

		CreateLogo();

		g.opened_at = Now();
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
		if (g.window == R_INVALID_ID)
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
		if (g.window == R_INVALID_ID || !g.visible)
			return;
		Rwindow_hide(g.window);
		g.visible = false;
	}


	void Show()
	{
		if (g.window == R_INVALID_ID || g.visible)
			return;

		// Shown transparent, then faded in : never an empty frame.
		Rwindow_set_opacity(g.window, 0.f);
		Rwindow_show(g.window);
		g.visible = true;
		Pump();

		const double start = Now();
		for (;;)
		{
			const double t = (Now() - start) / kFadeInSeconds;
			if (t >= 1.0)
				break;
			Rwindow_set_opacity(g.window, static_cast<float>(t));
			Pump();
			SleepMs(8);
		}

		Rwindow_set_opacity(g.window, 1.f);
		Pump();
	}


	void Complete()
	{
		if (g.window == R_INVALID_ID)
			return;

		Show();

		g.step = "Opening the editor...";

		// The bar fills up to 100 %, and the splash stays at least
		// kMinVisibleSeconds on the screen.
		const double now = Now();
		const double remaining = kMinVisibleSeconds - (now - g.opened_at);
		const double duration = std::max(kFillSeconds, remaining);
		const float from = g.shown;

		for (;;)
		{
			const double t = (Now() - now) / duration;
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
		Rwindow_set_topmost(g.window, R_TRUE);

		g.phase = Phase::Completed;
		g.editor_frames = 0;
	}


	void Tick()
	{
		if (g.window == R_INVALID_ID || g.phase == Phase::Loading)
			return;

		if (g.phase == Phase::Completed)
		{
			// The first editor frames (dock layout, fonts...) stay hidden.
			if (++g.editor_frames < kEditorFramesBeforeFade)
				return;
			g.phase = Phase::FadingOut;
			g.fade_start = Now();
		}

		const double t = (Now() - g.fade_start) / kFadeOutSeconds;
		if (t >= 1.0)
		{
			Close();
			return;
		}

		// The editor loop draws the frames (Rdraw_frame) : only the opacity changes here.
		Rwindow_set_opacity(g.window, 1.f - EaseOut(static_cast<float>(t)));
	}


	void Close()
	{
		if (g.window == R_INVALID_ID)
			return;

		if (g.own_logo)
			Rtexture_destroy(g.logo);
		Rwindow_destroy(g.window);

		g = State{};
	}


	bool IsOpen()
	{
		return g.window != R_INVALID_ID;
	}
}
