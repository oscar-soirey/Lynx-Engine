// =============================================================================
// Lynx Editor (Raven) : modules whose window is not in Raven yet.
// -----------------------------------------------------------------------------
// The ImGui editor had these windows ; their Raven version comes with the next
// phases of the port :
//   phase 2 : Git, Input Settings, code editors, Profiler,
//             Ship Game, Sprite -> voxels ;
//   phase 3 : the editor modules of the plugins (new Raven API).
// The Widget Editor (WidgetEditor.inl) and the Particle Editor
// (ParticleEditor.inl) are in Raven.
// Until then the editor keeps working without them : the files they would
// open are opened with the program Windows associates with them, and their
// menu entries say that the window is not available yet.
// =============================================================================

#include "../commands/CommandsWindow.h"
#include "../EditorPlugins.h"
#include "../GraphEditors.h"
#include "../ParticleEditor.h"
#include "../ProfilerWindow.h"
#include "../ScriptEditors.h"
#include "../ShipGame.h"
#include "../SpriteVoxelizer.h"

// The templates of the new files are the ones of the real editors.
#include "../../core/Particles.h"
#include "../../gameplay/AnimGraph.h"
#include "../../gameplay/BehaviorTree.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace
{
    std::string LowerExtension(const std::filesystem::path& path)
    {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext;
    }

    // The program Windows associates with the file (Notepad, VS Code...).
    void OpenWithSystem(const std::filesystem::path& path)
    {
#ifdef _WIN32
        ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
        std::cout << "[EDITOR] Open " << path.string() << "\n";
#endif
    }

    void NotYet(const char* window)
    {
        std::cout << "[EDITOR] The " << window << " window is not available in this version of the editor yet.\n";
    }
}


// ---- Commands (Python / Lynxie) ---------------------------------------------
// The real window (commands/CommandsWindow.cpp, ImGui), in a native window of
// its own : raven/CommandsHost.cpp.


// ---- Code editors : the files open in the program of the system --------------
namespace lynx::editor::script_editors
{
    void SetProjectRoot(const std::filesystem::path&) {}

    bool CanOpen(const std::filesystem::path& path)
    {
        static const char* const kText[] = {
            ".js", ".mjs", ".ts", ".json", ".txt", ".md", ".xml", ".py", ".ini", ".cfg", ".csv",
            ".glsl", ".vert", ".frag", ".shader", ".lua", ".yaml", ".yml", ".html", ".css",
        };
        const std::string ext = LowerExtension(path);
        return std::any_of(std::begin(kText), std::end(kText), [&](const char* e) { return ext == e; });
    }

    void Open(const std::filesystem::path& path, int) { OpenWithSystem(path); }
    void DrawAll(unsigned int) {}
    bool HasKeyboardFocus() { return false; }
    void PathMoved(const std::filesystem::path&, const std::filesystem::path&) {}
    void PathDeleted(const std::filesystem::path&) {}
    bool HasUnsavedChanges() { return false; }
    void SaveAll() {}
}


// ---- Widget Editor : raven/WidgetEditor.inl (Raven) ------------------------------


// ---- Anim Graph / Behavior Tree : raven/GraphEditors.inl (Raven NodeGraph) ------------


// ---- Particle Editor : raven/ParticleEditor.inl (Raven) ------------------------------


// ---- Editor modules of the plugins (new Raven API : phase 3) ----------------------
namespace lynx::editor::plugins
{
    namespace
    {
        Host g_host;
        std::vector<NewFileKind> g_no_kinds;
    }

    void SetHost(const Host& host) { g_host = host; }

    void LoadEditorModules()
    {
        std::cout << "[PLUGINS] Editor modules (<Name>.Editor.dll) are not loaded by this editor yet : "
                     "the runtime part of the plugins works.\n";
    }

    void UnloadEditorModules() {}
    void BeforeLevelSnapshot() {}
    void Tick(float) {}
    void DrawWindows() {}
    bool HasFocus() { return false; }
    void DrawWindowsMenuItems() {}
    void DrawPluginsMenu(bool*) {}
    bool HasMenuItems() { return false; }
    bool OpenFile(const std::filesystem::path&) { return false; }
    bool CanOpen(const std::filesystem::path&) { return false; }
    const std::vector<NewFileKind>& GetNewFileKinds() { return g_no_kinds; }
    void DrawManager(bool*) {}
    void DrawManagerContent() {}
}


