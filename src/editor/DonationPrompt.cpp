#include "DonationPrompt.h"

#include "raven/RavenUi.h"

#include <chrono>
#include <memory>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace lynx::editor::donation
{
	namespace
	{
		struct Settings
		{
			bool never = false;
			int amount = kDefaultAmount;
		};

		Settings g_settings;
		Rid g_window = R_INVALID_ID;
		std::function<bool()> g_busy;
		bool g_open = false;        // the dialog is on screen
		bool g_pending = false;     // to show as soon as possible
		bool g_forced = false;      // Show() : even with "never"
		double g_elapsed = 0.0;     // seconds since Start
		double g_next = 0.0;        // g_elapsed at which it is shown again
		double g_last_shown = -1e9;

		constexpr double kTick = 1.0;
		constexpr double kMomentGapSeconds = 20.0 * 60.0;
		constexpr int kMinAmount = 1;
		constexpr int kMaxAmount = 10000;

		std::filesystem::path SettingsFile()
		{
			std::filesystem::path dir;
#ifdef _WIN32
			if (const char* appdata = std::getenv("APPDATA"))
				dir = std::filesystem::path(appdata) / "Lynx";
#else
			if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
				dir = std::filesystem::path(xdg) / "lynx";
			else if (const char* home = std::getenv("HOME"))
				dir = std::filesystem::path(home) / ".config" / "lynx";
#endif
			if (dir.empty())
				dir = std::filesystem::current_path();
			return dir / "donation.cfg";
		}

		void Load()
		{
			std::ifstream file(SettingsFile());
			if (!file)
				return;
			std::string key;
			while (file >> key)
			{
				int v = 0;
				if (!(file >> v))
					break;
				if (key == "never")
					g_settings.never = v != 0;
				else if (key == "amount" && v >= kMinAmount && v <= kMaxAmount)
					g_settings.amount = v;
			}
		}

		void Save()
		{
			const std::filesystem::path path = SettingsFile();
			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);
			std::ofstream file(path, std::ios::trunc);
			if (!file)
			{
				std::cerr << "[DONATION] Could not write " << path.string() << "\n";
				return;
			}
			file << "never " << (g_settings.never ? 1 : 0) << "\n";
			file << "amount " << g_settings.amount << "\n";
		}

		// "12", " 12 ", "$12", "12.00" -> 12 ; -1 when it is not an amount.
		int ParseAmount(const std::string& text)
		{
			std::string digits;
			bool dot = false;
			for (char c : text)
			{
				if (c == ' ' || c == '$')
					continue;
				if (c == '.' || c == ',')
				{
					dot = true;
					continue;
				}
				if (c < '0' || c > '9')
					return -1;
				if (!dot)
					digits += c;
			}
			if (digits.empty() || digits.size() > 6)
				return -1;
			const int v = std::atoi(digits.c_str());
			return (v >= kMinAmount && v <= kMaxAmount) ? v : -1;
		}

		std::string SponsorUrl(int amount)
		{
			// One-time sponsorship with the amount already filled in.
			return std::string("https://github.com/sponsors/") + kSponsorUser +
			       "/sponsorships?frequency=one-time&amount=" + std::to_string(amount);
		}

		void OpenUrl(const std::string& url)
		{
			std::cout << "[DONATION] Opening " << url << "\n";
#ifdef _WIN32
			ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
			std::system(("open \"" + url + "\" &").c_str());
#else
			std::system(("xdg-open \"" + url + "\" >/dev/null 2>&1 &").c_str());
#endif
		}

		void OpenDialog()
		{
			if (g_open || g_window == R_INVALID_ID || !Rwindow_is_valid(g_window))
				return;

			Rid box = ui::Load("dialogs/donation.xml");
			if (box == R_INVALID_ID)
				return;

			g_open = true;
			g_last_shown = g_elapsed;

			struct State { int amount = kDefaultAmount; };
			auto state = std::make_shared<State>();
			state->amount = g_settings.amount;

			Rid input = ui::Find(box, "don.amount");
			Rid error = ui::Find(box, "don.error");
			Rid donate = ui::Find(box, "don.donate");
			Rid never = ui::Find(box, "don.never");

			auto set_amount = [state, donate, error](int amount)
			{
				state->amount = amount;
				ui::SetVisible(error, amount < 0);
				ui::SetEnabled(donate, amount > 0);
				if (amount > 0)
					ui::SetText(donate, "Donate $" + std::to_string(amount));
				else
					ui::SetText(error, "Enter an amount between $" + std::to_string(kMinAmount) +
					                   " and $" + std::to_string(kMaxAmount) + ".");
			};

			ui::SetText(input, std::to_string(state->amount));
			set_amount(state->amount);
			ui::SetChecked(never, g_settings.never);

			ui::OnText(input, [set_amount](const std::string& text) { set_amount(ParseAmount(text)); });
			for (int preset : { 3, 5, 10, 20 })
			{
				const std::string name = "don." + std::to_string(preset);
				ui::OnClick(ui::Find(box, name.c_str()), [input, set_amount, preset]()
				{
					ui::SetText(input, std::to_string(preset));
					set_amount(preset);
				});
			}
			ui::OnToggle(never, [](bool on)
			{
				g_settings.never = on;
				Save();
			});
			ui::OnClick(ui::Find(box, "don.later"), [box]() { Rdialog_close(box, 0); });
			ui::OnClick(donate, [box, state]()
			{
				if (state->amount <= 0)
					return;
				g_settings.amount = state->amount;
				Save();
				OpenUrl(SponsorUrl(state->amount));
				Rdialog_close(box, 1);
			});

			Rdialog_open(g_window, box, [](Rid, int, void*)
			{
				g_open = false;
				g_next = g_elapsed + kRepeatMinutes * 60.0;
			}, nullptr);
		}

		void Tick()
		{
			g_elapsed += kTick;
			if (g_open)
				return;
			if (g_elapsed >= g_next)
				g_pending = true;
			if (!g_pending)
				return;
			if (g_settings.never && !g_forced)
			{
				g_pending = false;
				return;
			}
			if (g_busy && g_busy())
				return;   // playing : wait for Stop
			g_pending = false;
			g_forced = false;
			OpenDialog();
		}
	}


	void Start(Rid window, std::function<bool()> busy)
	{
		static bool started = false;
		g_window = window;
		g_busy = std::move(busy);
		if (started)
			return;
		started = true;

		Load();
		g_next = kFirstDelaySeconds;
		ui::Every(kTick, Tick);
	}

	void Moment()
	{
		if (g_settings.never || g_open)
			return;
		if (g_elapsed - g_last_shown < kMomentGapSeconds)
			return;
		g_pending = true;
	}

	void Show()
	{
		g_pending = true;
		g_forced = true;
	}
}
