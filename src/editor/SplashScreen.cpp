#include "SplashScreen.h"

#include "raven/RavenUi.h"
#include "../core/Engine.h"

#include <Raven.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
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
		constexpr int kWidth = 760;
		constexpr int kHeight = 430;

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

		constexpr Color kBase = Rgb(14, 12, 12);          // behind the artwork
		constexpr Color kText = Rgb(245, 242, 238);
		constexpr Color kTextDim = Rgb(170, 162, 155);
		constexpr Color kTextFaint = Rgb(120, 113, 107);
		constexpr Color kTrack = Rgb(255, 255, 255, 28);
		constexpr Color kBarFrom = Rgb(240, 160, 40);     // Lynx orange
		constexpr Color kBarTo = Rgb(226, 70, 120);       // magenta
		constexpr Color kFrame = Rgb(255, 255, 255, 22);

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
			Rid artwork = R_INVALID_ID;   // abstract background (MakeArtwork)

			std::string project_name;
			std::string project_path;
			std::string step = "Starting...";
			std::string title;            // "Lynx 2027"
			std::string version;          // "Version 27.0.0"

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

		float Smoothstep(float a, float b, float x)
		{
			const float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
			return t * t * (3.f - 2.f * t);
		}

		Color Mix(Color a, Color b, float t)
		{
			return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
		}

		// "Lynx 2027" / "Version 27.0.0" from the engine version (CMakeLists.txt :
		// LYNX_VERSION_YEAR.MAJOR.MINOR, the year on two digits).
		void MakeTitle()
		{
			const std::string version = lynx::GetEngineVersion();
			int year = std::atoi(version.c_str());
			if (year > 0 && year < 100)
				year += 2000;
			g.title = year > 0 ? "Lynx " + std::to_string(year) : std::string("Lynx");
			g.version = "Version " + version;
		}

		// ---- artwork ---------------------------------------------------------------
		// An abstract picture computed once, in the spirit of the JetBrains / Adobe
		// splash screens : glowing color fields (warped), silky bands, film grain.
		// Lynx colors (orange, magenta, violet) ; the project name moves the shapes
		// a little, so that every project has its own picture. The left and the
		// bottom stay dark : the texts are drawn there.

		uint32_t Hash(const std::string& text)
		{
			uint32_t h = 2166136261u;   // FNV-1a
			for (unsigned char c : text)
				h = (h ^ c) * 16777619u;
			return h;
		}

		struct Random
		{
			uint32_t state;
			float Next()   // [0, 1)
			{
				state ^= state << 13; state ^= state >> 17; state ^= state << 5;
				return static_cast<float>(state & 0xFFFFFF) / 16777216.f;
			}
			float Range(float a, float b) { return a + (b - a) * Next(); }
		};

		Rid MakeArtwork(int width, int height, uint32_t seed)
		{
			Random rnd{ seed ? seed : 0x9E3779B9u };

			struct Blob { float x, y, radius; float r, g, b; };
			const float aspect = static_cast<float>(width) / static_cast<float>(height);
			Blob blobs[] = {
				{ aspect * 0.72f, 0.22f, 0.42f, 0.95f, 0.52f, 0.10f },   // orange
				{ aspect * 0.92f, 0.08f, 0.30f, 1.00f, 0.78f, 0.32f },   // amber
				{ aspect * 0.95f, 0.70f, 0.46f, 0.86f, 0.16f, 0.42f },   // magenta
				{ aspect * 0.62f, 0.95f, 0.40f, 0.36f, 0.18f, 0.86f },   // violet
				{ aspect * 0.50f, 0.45f, 0.30f, 0.62f, 0.07f, 0.12f },   // deep red
			};
			for (Blob& b : blobs)
			{
				b.x += rnd.Range(-0.10f, 0.10f);
				b.y += rnd.Range(-0.10f, 0.10f);
				b.radius *= rnd.Range(0.85f, 1.15f);
			}
			const float s1 = rnd.Range(0.f, 6.28f), s2 = rnd.Range(0.f, 6.28f);
			const float s3 = rnd.Range(0.f, 6.28f), s4 = rnd.Range(0.f, 6.28f);
			const float band_angle = rnd.Range(0.7f, 1.1f);

			std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
			Random grain{ seed ^ 0xA5A5A5A5u };

			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					const float u = static_cast<float>(x) / static_cast<float>(width);
					const float v = static_cast<float>(y) / static_cast<float>(height);
					const float px = static_cast<float>(x) / static_cast<float>(height);
					const float py = v;

					// Domain warp : organic, flowing shapes.
					const float wx = px + 0.09f * std::sin(py * 5.5f + s1) + 0.05f * std::sin((px + py) * 9.f + s2);
					const float wy = py + 0.09f * std::cos(px * 4.5f + s3) + 0.04f * std::sin((px - py) * 11.f + s4);

					float r = 0.f, gg = 0.f, b = 0.f;
					for (const Blob& blob : blobs)
					{
						const float dx = wx - blob.x, dy = wy - blob.y;
						const float w = std::exp(-(dx * dx + dy * dy) / (blob.radius * blob.radius) * 2.4f);
						r += blob.r * w; gg += blob.g * w; b += blob.b * w;
					}

					// Silky bands across the color fields.
					const float along = wx * std::cos(band_angle) - wy * std::sin(band_angle);
					const float band = std::pow(0.5f + 0.5f * std::sin(along * 26.f + s2), 28.f);
					const float glow = std::min(1.f, (r + gg + b) * 0.6f);
					r += band * glow * 0.35f; gg += band * glow * 0.28f; b += band * glow * 0.30f;

					// Soft tone mapping : glows, never clips.
					r = 1.f - std::exp(-r * 1.25f);
					gg = 1.f - std::exp(-gg * 1.25f);
					b = 1.f - std::exp(-b * 1.25f);

					// Dark on the left (title) and at the bottom (project, progress).
					const float art = Smoothstep(0.18f, 0.80f, u) * (1.f - 0.65f * Smoothstep(0.55f, 1.f, v));
					r = kBase.r + (r - kBase.r) * art;
					gg = kBase.g + (gg - kBase.g) * art;
					b = kBase.b + (b - kBase.b) * art;

					const float noise = (grain.Next() - 0.5f) * 0.035f;
					uint8_t* out = &pixels[(static_cast<size_t>(y) * width + x) * 4];
					out[0] = static_cast<uint8_t>(std::clamp(r + noise, 0.f, 1.f) * 255.f + 0.5f);
					out[1] = static_cast<uint8_t>(std::clamp(gg + noise, 0.f, 1.f) * 255.f + 0.5f);
					out[2] = static_cast<uint8_t>(std::clamp(b + noise, 0.f, 1.f) * 255.f + 0.5f);
					out[3] = 255;
				}
			}

			return Rtexture_create_from_pixels(pixels.data(), width, height);
		}

		// ---- drawing -------------------------------------------------------------

		void Rect(RPainter* p, float x, float y, float w, float h, Color c, float radius = 0.f)
		{
			if (w > 0.f && h > 0.f)
				Rdraw_rect(p, x, y, w, h, radius, c.r, c.g, c.b, c.a);
		}

		void Text(RPainter* p, float size, float x, float y, const std::string& text, Color c)
		{
			Rdraw_text(p, R_INVALID_ID, size, x, y, text.c_str(), c.r, c.g, c.b, c.a);
		}

		float TextWidth(float size, const std::string& text)
		{
			float w = 0.f, h = 0.f;
			Rfont_measure_text(R_INVALID_ID, size, text.c_str(), &w, &h);
			if (w <= 0.f)   // no font yet : a rough estimate
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

		// Thin bar along the bottom : orange -> magenta, a light running inside.
		void DrawProgressBar(RPainter* p, float x, float y, float w, float h, float value, double time)
		{
			Rect(p, x, y, w, h, kTrack, h * 0.5f);

			const float fill = std::floor(w * std::clamp(value, 0.f, 1.f));
			if (fill <= 0.f)
				return;

			Rdraw_push_clip_rect(p, x, y, fill, h);
			const int slices = 48;
			const float slice_w = w / static_cast<float>(slices);
			for (int i = 0; i < slices; ++i)
			{
				const float sx = x + slice_w * static_cast<float>(i);
				if (sx >= x + fill)
					break;
				Rect(p, std::floor(sx), y, std::ceil(slice_w) + 1.f, h,
				     Mix(kBarFrom, kBarTo, static_cast<float>(i) / static_cast<float>(slices - 1)));
			}
			const float shine_x = x - 80.f + static_cast<float>(std::fmod(time * 0.7, 1.0)) * (fill + 160.f);
			Rect(p, std::floor(shine_x), y, 80.f, h, Rgb(255, 240, 220, 90));
			Rdraw_pop_clip_rect(p);
		}

		void Draw(Rid, RPainter* p, float width, float height, void*)
		{
			const double time = Now();

			// Artwork, full window, and a hairline frame.
			Rect(p, 0, 0, width, height, kBase);
			if (g.artwork != R_INVALID_ID)
				Rdraw_image(p, g.artwork, 0.f, 0.f, width, height, 1.f, 1.f, 1.f, 1.f);
			Rect(p, 0, 0, width, 1.f, kFrame);
			Rect(p, 0, height - 1.f, width, 1.f, kFrame);
			Rect(p, 0, 0, 1.f, height, kFrame);
			Rect(p, width - 1.f, 0, 1.f, height, kFrame);

			const float pad = 40.f;
			const float content_w = width - pad * 2.f;

			// Lynxie, "Lynx 2027", the exact version in small.
			const float logo_h = 40.f;
			const float logo_w = std::floor(logo_h * g.logo_ratio);
			float x = pad;
			if (g.logo != R_INVALID_ID)
			{
				Rdraw_image(p, g.logo, pad, pad + 6.f, logo_w, logo_h, kText.r, kText.g, kText.b, 1.f);
				x += logo_w + 16.f;
			}
			const float title_size = 52.f;
			Text(p, title_size, x, pad, g.title, kText);
			Text(p, 13.f, x + 2.f, pad + title_size + 6.f, g.version, kTextDim);

			// Bottom : project, current step, progress.
			const float bar_h = 4.f;
			const float bar_y = height - pad + 4.f;
			const float step_size = 13.f;
			const float step_y = bar_y - 12.f - step_size;

			char percent[16];
			std::snprintf(percent, sizeof(percent), "%d%%",
			              static_cast<int>(std::round(std::clamp(g.shown, 0.f, 1.f) * 100.f)));
			const float percent_w = TextWidth(step_size, percent);

			const float path_size = 12.f;
			const float path_y = step_y - 22.f - path_size;
			const float name_size = 24.f;
			const float name_y = path_y - 6.f - name_size;

			Text(p, name_size, pad, name_y, FitRight(g.project_name, name_size, content_w * 0.7f), kText);
			Text(p, path_size, pad, path_y, FitLeft(g.project_path, path_size, content_w * 0.7f), kTextFaint);

			Text(p, step_size, pad, step_y, FitRight(g.step, step_size, content_w - percent_w - 16.f), kTextDim);
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
		Rwindow_set_clear_color(g.window, kBase.r, kBase.g, kBase.b, 1.f);
		Rwindow_center(g.window);

		g.canvas = Rwidget_create(R_WIDGET_CUSTOM);
		Rcustom_set_paint_callback(g.canvas, Draw, nullptr);
		Rwindow_set_content(g.window, g.canvas);
		Rwindow_set_continuous_redraw(g.window, R_TRUE);   // shine of the bar

		CreateLogo();
		MakeTitle();
		g.artwork = MakeArtwork(kWidth, kHeight, Hash(g.project_name));

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
		if (g.artwork != R_INVALID_ID)
			Rtexture_destroy(g.artwork);
		Rwindow_destroy(g.window);

		g = State{};
	}


	bool IsOpen()
	{
		return g.window != R_INVALID_ID;
	}
}
