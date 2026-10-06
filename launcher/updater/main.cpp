// LynxUpdater : se lance avant le launcher (même dossier).
//   1. lit le tag installé (launcher.version) et la dernière release GitHub « launcher* » ;
//   2. si une version plus récente existe : télécharge et remplace le launcher
//      (petite fenêtre de progression, affichée seulement dans ce cas) ;
//   3. lance LynxLauncher.exe.
// Sans réseau ou en cas d'échec de la mise à jour, le launcher déjà installé est lancé quand même.

#include "Updater.h"
#include "../Process.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
	#include <commctrl.h>
#endif

namespace fs = std::filesystem;

namespace
{
	fs::path ExecutablePath()
	{
#ifdef _WIN32
		wchar_t buffer[MAX_PATH];
		DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		if (length > 0 && length < MAX_PATH)
			return fs::path(buffer);
#endif
		return fs::current_path() / "LynxUpdater.exe";
	}

#ifdef _WIN32
	std::wstring Widen(const std::string& text)
	{
		if (text.empty())
			return {};
		int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
		std::wstring result(size, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.data(), size);
		return result;
	}

	void ShowError(const std::string& text)
	{
		MessageBoxW(nullptr, Widen(text).c_str(), L"Lynx updater", MB_OK | MB_ICONERROR);
	}

	// État partagé entre le thread de téléchargement et la fenêtre (lu par un timer).
	struct SharedState
	{
		std::mutex mutex;
		std::string status = "Recherche de mises à jour...";
		std::atomic<float> progress{ 0.0f };
		std::atomic<bool> done{ false };
	};
	SharedState g_state;
	HWND g_label = nullptr;
	HWND g_bar = nullptr;

	LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
	{
		switch (message)
		{
		case WM_TIMER:
		{
			std::string status;
			{
				std::lock_guard<std::mutex> lock(g_state.mutex);
				status = g_state.status;
			}
			SetWindowTextW(g_label, Widen(status).c_str());
			SendMessageW(g_bar, PBM_SETPOS, WPARAM(g_state.progress.load() * 1000.0f), 0);
			if (g_state.done)
				DestroyWindow(window);
			return 0;
		}
		case WM_CLOSE:
			return 0;   // pas de fermeture pendant la mise à jour (un launcher à moitié copié ne démarrerait pas)
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		}
		return DefWindowProcW(window, message, wparam, lparam);
	}

	// Affiche la fenêtre tant que `work` tourne dans un thread. Bloquant.
	void RunWithWindow(const std::function<void()>& work)
	{
		INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_PROGRESS_CLASS };
		InitCommonControlsEx(&controls);

		HINSTANCE instance = GetModuleHandleW(nullptr);
		WNDCLASSW window_class{};
		window_class.lpfnWndProc = WindowProc;
		window_class.hInstance = instance;
		window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
		window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
		window_class.lpszClassName = L"LynxUpdaterWindow";
		RegisterClassW(&window_class);

		const int width = 460, height = 130;
		RECT area{};
		SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
		HWND window = CreateWindowExW(WS_EX_TOPMOST, window_class.lpszClassName, L"Lynx - mise à jour du launcher",
			WS_CAPTION | WS_OVERLAPPED,   // pas de WS_SYSMENU : pas de croix
			area.left + (area.right - area.left - width) / 2, area.top + (area.bottom - area.top - height) / 2,
			width, height, nullptr, nullptr, instance, nullptr);

		HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
		g_label = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
			16, 14, width - 48, 22, window, nullptr, instance, nullptr);
		g_bar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE,
			16, 44, width - 48, 20, window, nullptr, instance, nullptr);
		SendMessageW(g_label, WM_SETFONT, WPARAM(font), TRUE);
		SendMessageW(g_bar, PBM_SETRANGE32, 0, 1000);

		ShowWindow(window, SW_SHOW);
		SetTimer(window, 1, 50, nullptr);

		std::thread worker([&] {
			work();
			g_state.done = true;
		});

		MSG message;
		while (GetMessageW(&message, nullptr, 0, 0) > 0)
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		worker.join();
	}
#endif
}

int main(int, char**)
{
	const fs::path self = ExecutablePath();
	const fs::path directory = self.parent_path();
	const fs::path launcher = directory / Updater::kLauncherExe;

	std::error_code error;
	const bool launcher_present = fs::is_regular_file(launcher, error);
	const std::string installed_tag = Updater::ReadInstalledTag(directory);

	// 1. Quelle est la dernière version ? (quelques centaines de ms, sans fenêtre)
	Updater::Release latest;
	std::string fetch_error;
	const bool fetched = Updater::FetchLatest(latest, fetch_error);

	// 2. Mise à jour si le launcher est absent, de version inconnue, ou plus ancien
	const bool needs_update = fetched &&
		(!launcher_present || installed_tag.empty() ||
		 Updater::Compare(latest.version, Updater::ParseVersion(installed_tag)) > 0);

	std::string update_error;
	bool update_failed = false;
	if (needs_update)
	{
#ifdef _WIN32
		RunWithWindow([&] {
			auto status = [](const std::string& text) {
				std::lock_guard<std::mutex> lock(g_state.mutex);
				g_state.status = text;
			};
			auto progress = [](float value) { g_state.progress = value; };
			update_failed = !Updater::Install(latest, directory, self.filename().string(), status, progress, update_error);
		});
#else
		update_failed = !Updater::Install(latest, directory, self.filename().string(), [](const std::string&) {}, [](float) {}, update_error);
#endif
	}

	// 3. Lancement du launcher (même si la mise à jour a échoué : l'ancienne version reste utilisable)
	if (!fs::is_regular_file(launcher, error))
	{
		std::string message = std::string(Updater::kLauncherExe) + " est introuvable à côté de l'updater.";
		if (!fetched)
			message += "\n\n" + fetch_error;
		if (update_failed)
			message += "\n\n" + update_error;
#ifdef _WIN32
		ShowError(message);
#endif
		return 1;
	}

#ifdef _WIN32
	if (update_failed)
		ShowError("La mise à jour du launcher a échoué, l'ancienne version va être lancée.\n\n" + update_error);
#endif

	if (!Process::Launch(launcher, directory))
	{
#ifdef _WIN32
		ShowError("Impossible de lancer " + launcher.string());
#endif
		return 1;
	}
	return 0;
}
