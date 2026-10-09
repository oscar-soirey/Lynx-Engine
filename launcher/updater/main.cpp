// LynxUpdater : se lance avant le launcher. Il est autonome : seul dans son dossier
// (installateur), il télécharge le launcher.
//   1. choisit le dossier du launcher : à côté de l'updater si ce dossier est modifiable,
//      sinon %LOCALAPPDATA%\Lynx\launcher (installation dans Program Files : l'updater
//      tourne sans droits administrateur et ne peut pas y écrire) ;
//   2. lit le tag installé (launcher.version) et la dernière release GitHub « launcher* » ;
//   3. si le launcher est absent ou plus ancien : télécharge et installe la dernière version
//      (petite fenêtre de progression, affichée seulement dans ce cas) ;
//   4. lance LynxLauncher.exe.
// Sans réseau ou en cas d'échec de la mise à jour, le launcher déjà installé est lancé quand même.

#include "Updater.h"
#include "../Process.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

	// Peut-on créer des fichiers dans ce dossier ? (Program Files : non, sans droits admin)
	bool IsWritable(const fs::path& directory)
	{
		std::error_code error;
		fs::create_directories(directory, error);
		const fs::path probe = directory / ".lynx-write-test";
		{
			std::ofstream file(probe, std::ios::trunc);
			if (!file)
				return false;
		}
		fs::remove(probe, error);
		return true;
	}

	// Dossier du launcher quand celui de l'updater n'est pas modifiable.
	fs::path UserLauncherDirectory()
	{
#ifdef _WIN32
		if (const char* base = std::getenv("LOCALAPPDATA"))
			return fs::path(base) / "Lynx" / "launcher";
#endif
		if (const char* home = std::getenv("HOME"))
			return fs::path(home) / ".local" / "share" / "Lynx" / "launcher";
		return fs::temp_directory_path() / "Lynx" / "launcher";
	}

	bool HasLauncher(const fs::path& directory)
	{
		std::error_code error;
		return fs::is_regular_file(directory / Updater::kLauncherExe, error);
	}

	// Mise à jour nécessaire si le launcher est absent, de version inconnue, ou plus ancien.
	bool NeedsUpdate(const fs::path& directory, const Updater::Release& latest)
	{
		if (!HasLauncher(directory))
			return true;
		const std::string installed = Updater::ReadInstalledTag(directory);
		return installed.empty() || Updater::Compare(latest.version, Updater::ParseVersion(installed)) > 0;
	}

	// GitHub peut ne pas répondre du premier coup (réseau qui s'éveille, limite de l'API).
	bool FetchLatestWithRetries(Updater::Release& out, std::string& error, int attempts)
	{
		for (int i = 0; i < attempts; ++i)
		{
			if (Updater::FetchLatest(out, error))
				return true;
			if (i + 1 < attempts)
				std::this_thread::sleep_for(std::chrono::seconds(2));
		}
		return false;
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
	const fs::path self_directory = self.parent_path();
	const fs::path user_directory = UserLauncherDirectory();

	// 1. Dossier du launcher : à côté de l'updater si possible, sinon dans le profil
	//    de l'utilisateur. Un launcher déjà présent dans le profil est prioritaire
	//    (installation précédente dans Program Files).
	fs::path directory = self_directory;
	if (!IsWritable(self_directory) && (HasLauncher(user_directory) || !HasLauncher(self_directory)))
		directory = user_directory;

	const bool first_install = !HasLauncher(directory);

	Updater::Release latest;
	std::string fetch_error;
	bool fetched = false;
	bool update_failed = false;
	std::string update_error;

	// 3. Installation / mise à jour de `latest` dans `directory`.
	auto install_latest = [&](const Updater::StatusFn& status, const Updater::ProgressFn& progress) {
		// Launcher présent mais dans un dossier non modifiable : la nouvelle version
		// va dans le profil de l'utilisateur.
		if (!IsWritable(directory))
			directory = user_directory;
		update_failed = !Updater::Install(latest, directory, self.filename().string(), status, progress, update_error);
	};

	// Télécharge (si besoin) la dernière version dans `directory`.
	auto check_and_update = [&](const Updater::StatusFn& status, const Updater::ProgressFn& progress) {
		// 2. Quelle est la dernière version ? (quelques centaines de ms)
		status("Recherche de la dernière version du launcher...");
		fetched = FetchLatestWithRetries(latest, fetch_error, first_install ? 3 : 1);
		if (!fetched || !NeedsUpdate(directory, latest))
			return;

		install_latest(status, progress);
	};

#ifdef _WIN32
	auto status = [](const std::string& text) {
		std::lock_guard<std::mutex> lock(g_state.mutex);
		g_state.status = text;
	};
	auto progress = [](float value) { g_state.progress = value; };

	if (first_install)
	{
		// Pas encore de launcher : la fenêtre s'affiche tout de suite (recherche + téléchargement).
		RunWithWindow([&] { check_and_update(status, progress); });
	}
	else
	{
		// Launcher présent : recherche sans fenêtre, fenêtre seulement s'il faut télécharger.
		fetched = FetchLatestWithRetries(latest, fetch_error, 1);
		if (fetched && NeedsUpdate(directory, latest))
		{
			RunWithWindow([&] { install_latest(status, progress); });
		}
	}
#else
	check_and_update([](const std::string&) {}, [](float) {});
#endif

	// 4. Lancement du launcher (même si la mise à jour a échoué : l'ancienne version reste utilisable)
	fs::path launcher = directory / Updater::kLauncherExe;
	if (!HasLauncher(directory))
	{
		// la mise à jour a échoué en voulant passer dans le profil : l'ancien launcher est peut-être à côté
		if (HasLauncher(self_directory))
			launcher = self_directory / Updater::kLauncherExe;
		else
		{
			std::string message = "Impossible d'installer le launcher Lynx.";
			if (!fetched)
				message += "\n\nLe premier lancement a besoin d'Internet pour télécharger le launcher depuis GitHub."
				           "\n" + fetch_error;
			if (update_failed)
				message += "\n\n" + update_error;
#ifdef _WIN32
			ShowError(message);
#endif
			return 1;
		}
	}

#ifdef _WIN32
	if (update_failed)
		ShowError("La mise à jour du launcher a échoué, l'ancienne version va être lancée.\n\n" + update_error);
#endif

	if (!Process::Launch(launcher, launcher.parent_path()))
	{
#ifdef _WIN32
		ShowError("Impossible de lancer " + launcher.string());
#endif
		return 1;
	}
	return 0;
}
