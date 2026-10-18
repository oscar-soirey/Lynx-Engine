// =============================================================================
// Lynx Editor
// -----------------------------------------------------------------------------
// Standalone executable of the engine (Raven panels, gizmo, brush, undo,
// content browser...). It edits a GAME PROJECT chosen at startup :
//
//   LynxEditor.exe                     -> project browser
//   LynxEditor.exe "C:/path/MyGame"    -> opens this project directly
//
// A project is a folder with assets/ and the game DLL in build/ (see
// host/GameProject.h). Once a project is opened, the working directory becomes
// the project root : "assets/...", *.level, settings.cfg, editor_camera.txt,
// imgui.ini... are the project's files.
//
// Everything specific to a game (voxel types, gameplay camera, collision debug)
// comes from the game DLL through the optional functions of
// core/GameModuleAPI.h.
// =============================================================================

#include "../Lynx.h"
#include "../core/GameModuleAPI.h"
// The interface is Raven (src/editor/raven, interface/*.xml embedded in the
// executable). GLFW stays for the gamepads and its key codes (no window).
#include <glfw/glfw3.h>
#include <Raven.h>
#include "raven/RavenUi.h"
#include "raven/GlBridge.h"
#include <hrl/hrl.h>
#include <hrl/hrl_gl.h>
#define STB_IMAGE_IMPLEMENTATION
#include "../../third-party/stb/stb_image.h"

#include <iostream>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <algorithm>
#include <set>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iomanip>
#include <sstream>
#include <random>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <memory>
#include <variant>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include "../audio/AudioCommon.h"
#include "../core/Private/SystemModule.h"
#include "../host/GamepadInput.h"
#include "../gameplay/Private/InputManager.h"
#include "../gameplay/CameraShake.h"

#include "../host/GameProject.h"
#include "../host/RenderBackend.h"
#include "InputSettingsEditor.h"
#include "GameBuild.h"
#include "ProjectBrowser.h"
#include "StartupBuild.h"
#include "SplashScreen.h"
#include "Keymap.h"
#include "commands/CommandRegistry.h"
#include "commands/CommandServer.h"
#include "commands/CommandUtils.h"
#include "commands/CommandsWindow.h"
#include "OutputConsole.h"
#include "EditorOptions.h"
#include "ExternalIde.h"
#include "ShellIntegration.h"
#include "EditorPlugins.h"
#include "ProjectTemplates.h"
#include "EditorIcons.h"
#include "ProfilerWindow.h"
#include "CommandPalette.h"
#include "ParticleEditor.h"
#include "ScriptEditors.h"
#include "WidgetEditor.h"
#include "VoxelAssemblyEditor.h"
#include "Documents.h"
#include "GraphEditors.h"
#include "VoxelTypeEditor.h"
#include "FileIcons.h"
#include "SpriteVoxelizer.h"
#include "ProjectInfo.h"
#include "ShipGame.h"
#include "DonationPrompt.h"
#include "commands/ScriptRunner.h"
#include <array>
#include <chrono>
#include <ctime>

bool isPlaying = false;

// Simulate mode (like Unreal's Simulate) : the game runs (isPlaying is true
// too) but nobody is possessed, the game gets no input and the viewport keeps
// the editor camera and its movements (Engine::SetSimulating).
bool isSimulating = false;

HRL_id scene;

// Project opened by the editor, and the optional functions exported by its
// game DLL (see core/GameModuleAPI.h). The hooks are cleared before the DLL
// is unloaded and loaded again right after.
static lynx::host::GameProject currentProject;
static lynx::game_api::Hooks gameHooks;

static const char* const kEditorWindowsFile = "editor_windows.txt";

struct EditorWindowVisibility
{
    bool viewport = true;
    bool outliner = true;
    bool placeActors = true;
    bool contentBrowser = true;
    bool details = true;
    bool colorPicking = false;
    bool config = true;          // Paint
    bool cameraShake = false;
    bool inputSettings = false;
    bool commands = true;
    bool git = false;
    bool console = true;
    bool profiler = true;        // records only while its tab is visible
    bool nodeGraphTest = false;  // (removed window : kept for the format of editor_windows.txt)
    bool postProcess = false;
    bool voxelWorld = false;     // Voxel World (size of the level's world)
    bool lighting2D = false;     // 2D Lighting (lynx::lighting2d)
};

static EditorWindowVisibility editorWindows;
static bool showEditorOptions = false;   // Options window (not saved)
static bool showPluginManager = false;   // Plugins window (not saved)

static void LoadEditorWindowVisibility()
{
    std::ifstream file(kEditorWindowsFile);
    if (!file) return;

    int viewport, outliner, placeActors, contentBrowser;
    int details, colorPicking, config, cameraShake;

    if (!(file >> viewport >> outliner >> placeActors >> contentBrowser
               >> details >> colorPicking >> config >> cameraShake))
        return;

    editorWindows.viewport = viewport != 0;
    editorWindows.outliner = outliner != 0;
    editorWindows.placeActors = placeActors != 0;
    editorWindows.contentBrowser = contentBrowser != 0;
    editorWindows.details = details != 0;
    editorWindows.colorPicking = colorPicking != 0;
    editorWindows.config = config != 0;
    editorWindows.cameraShake = cameraShake != 0;

    // Added later : optional, so older files still load.
    int inputSettings = 0;
    if (file >> inputSettings)
        editorWindows.inputSettings = inputSettings != 0;

    int commands = 0;
    if (file >> commands)
        editorWindows.commands = commands != 0;

    int git = 0;
    if (file >> git)
        editorWindows.git = git != 0;

    int console = 1;
    if (file >> console)
        editorWindows.console = console != 0;

    int profiler = 0;
    if (file >> profiler)
        editorWindows.profiler = profiler != 0;

    int nodeGraphTest = 0;
    if (file >> nodeGraphTest)
        editorWindows.nodeGraphTest = nodeGraphTest != 0;

    int postProcess = 0;
    if (file >> postProcess)
        editorWindows.postProcess = postProcess != 0;

    int voxelWorld = 0;
    if (file >> voxelWorld)
        editorWindows.voxelWorld = voxelWorld != 0;

    int lighting2D = 0;
    if (file >> lighting2D)
        editorWindows.lighting2D = lighting2D != 0;
}

static void SaveEditorWindowVisibility()
{
    std::ofstream file(kEditorWindowsFile, std::ios::trunc);
    if (!file) return;

    file << (editorWindows.viewport ? 1 : 0) << ' '
         << (editorWindows.outliner ? 1 : 0) << ' '
         << (editorWindows.placeActors ? 1 : 0) << ' '
         << (editorWindows.contentBrowser ? 1 : 0) << ' '
         << (editorWindows.details ? 1 : 0) << ' '
         << (editorWindows.colorPicking ? 1 : 0) << ' '
         << (editorWindows.config ? 1 : 0) << ' '
         << (editorWindows.cameraShake ? 1 : 0) << ' '
         << (editorWindows.inputSettings ? 1 : 0) << ' '
         << (editorWindows.commands ? 1 : 0) << ' '
         << (editorWindows.git ? 1 : 0) << ' '
         << (editorWindows.console ? 1 : 0) << ' '
         << (editorWindows.profiler ? 1 : 0) << ' '
         << (editorWindows.nodeGraphTest ? 1 : 0) << ' '
         << (editorWindows.postProcess ? 1 : 0) << ' '
         << (editorWindows.voxelWorld ? 1 : 0) << ' '
         << (editorWindows.lighting2D ? 1 : 0) << '\n';
}

// Command palette (Ctrl+P) : commands, windows, actors and files (CommandPalette.h).
static void OpenCommandPalette(const std::string& prefix = "");

// Camera Shake : "Test" shakes the editor camera (panel : raven/PanelTools.inl).
static void ApplyCameraShakePreview(HRL_id camera, float dt);




// =============================================================================
// Scene viewport window (editor)
// -----------------------------------------------------------------------------
// The "Viewport" ImGui window shows the scene texture. While it is visible,
// the scene is no longer under the whole window, so:
//   - the mouse is "for the scene" only when the Viewport window is hovered,
//   - mouse positions must be converted from window pixels to scene pixels.
// When the Viewport window is closed / hidden, everything behaves as before.
// =============================================================================

struct SceneViewportState
{
    bool visible = false;   // the Viewport window shows the scene this frame
    bool hovered = false;   // the mouse is over the image (or dragging from it)
    bool blocked = false;   // the current click started on a panel / popup : not for the scene
    float x = 0.f;          // image position, window pixels
    float y = 0.f;
    float w = 0.f;          // image size, window pixels
    float h = 0.f;
    int renderW = 0;         // HRL scene framebuffer size, physical pixels
    int renderH = 0;
    bool hasRenderSize = false;
};

static SceneViewportState sceneViewport;

// What the editor code reads of the keyboard / mouse (it was ImGuiIO) : the
// state of the editor window (raven/RavenUi.h, lynx::editor::input).
struct EditorIO
{
    bool KeyCtrl = false;
    bool KeyShift = false;
    bool KeyAlt = false;
    bool WantTextInput = false;
    // The scene viewport is a Raven widget : outside it the mouse belongs to the interface.
    bool WantCaptureMouse = true;
    struct { float x = 0.f, y = 0.f; } MouseDelta;

    static EditorIO Now()
    {
        namespace in = lynx::editor::input;
        EditorIO io;
        io.KeyCtrl = in::Ctrl();
        io.KeyShift = in::Shift();
        io.KeyAlt = in::Alt();
        io.WantTextInput = in::WantTextInput();
        in::MouseDelta(io.MouseDelta.x, io.MouseDelta.y);
        return io;
    }
};

// Replaces "!ImGui::GetIO().WantCaptureMouse" for every scene interaction.
static bool SceneAcceptsMouse(bool imgui_wants_mouse)
{
    if (sceneViewport.visible)
        return sceneViewport.hovered && !sceneViewport.blocked;

    return !imgui_wants_mouse;
}

// Window pixels -> scene pixels (what HRL picking / gizmo functions expect).
static void MouseToScene(
    double window_x,
    double window_y,
    double& scene_x,
    double& scene_y)
{
    if (!sceneViewport.visible ||
        sceneViewport.w <= 0.f ||
        sceneViewport.h <= 0.f)
    {
        scene_x = window_x;
        scene_y = window_y;
        return;
    }

    scene_x = (window_x - sceneViewport.x) * sceneViewport.renderW / sceneViewport.w;
    scene_y = (window_y - sceneViewport.y) * sceneViewport.renderH / sceneViewport.h;
}


static void ErrorCallback(
    HRL_EError code,
    HRL_ESeverity severity,
    const char* detail)
{
    printf(
        "Error of type : %s, Severity : %s, Details : %s\n",
        HRL_ErrorEnumToString(code),
        HRL_SeverityEnumToString(severity),
        detail
    );

    if (severity >= HRL_SEVERITY_FATAL)
    {
        exit(code);
    }
}


// Switches between "game running" and "game stopped". Shared by the editor
// (F3 toggle) and by the game-only build (which simply starts playing).
static void SetPlaying(
    lynx::Engine* engine,
    HRL_id viewport,
    HRL_id camera,
    bool playing)
{
    // The scene is seen through the viewport of the default player.
    if (lynx::PlayerController* player = engine->GetDefaultPlayer())
        player->SetViewCamera(camera);
    else
        lynx::camera_state::SetViewportCamera(viewport, camera);

    if (playing == isPlaying)
    {
        // Already in this mode (ex : first call) : only the camera changes.
        if (playing) engine->StartGame(); else engine->EndGame();
        return;
    }

    isPlaying = playing;

    if (playing)
    {
        engine->StartGame();

        if (gameHooks.on_game_start)
            gameHooks.on_game_start();
    }
    else
    {
        // The game stops first (music...), while its actors still exist.
        if (gameHooks.on_game_end)
            gameHooks.on_game_end();

        engine->EndGame();
    }
}


static std::string world_file_path_string;

// Level being edited (relative to assets/, ".level"). Its voxel world
// (world_file_path_string) is the one linked to it (Level::GetLinkedVoxelWorld).
static std::string current_level_file = "world.level";

namespace editor
{
    // Opens another level between two frames (defined below).
    void RequestOpenLevel(const std::string& level);
}

// Last level opened in the editor, reopened at the next start.
static const char* const kLastLevelFile = ".lynx/last_level.txt";

static void RememberLastLevel(const std::string& level)
{
    std::error_code error;
    std::filesystem::create_directories(".lynx", error);
    std::ofstream out(kLastLevelFile, std::ios::binary | std::ios::trunc);
    if (out)
        out << level;
}

static std::string ReadLastLevel()
{
    std::ifstream in(kLastLevelFile, std::ios::binary);
    std::string level;
    if (in)
        std::getline(in, level);
    while (!level.empty() && (level.back() == '\r' || level.back() == ' ' || level.back() == '\t'))
        level.pop_back();
    return level;
}

// Old projects : save_file.txt = voxel world, actors in world.xml. Turned
// into world.level (linked to that voxel world) ; world.xml is kept as
// world.xml.bak. save_file.txt then names world.level (startup level).
static std::string MigrateLegacyLevel(const std::string& legacy_level, const std::string& legacy_voxels)
{
    namespace sfs = std::filesystem;
    const sfs::path assets = "assets";
    const sfs::path level_disk = assets / "world.level";
    std::error_code error;

    if (!sfs::exists(level_disk, error))
    {
        const sfs::path old_disk = assets / legacy_level;

        if (legacy_level != "world.level" && sfs::exists(old_disk, error))
        {
            sfs::copy_file(old_disk, level_disk, error);
            if (!error)
            {
                std::error_code e2;
                sfs::rename(old_disk, sfs::path(old_disk.string() + ".bak"), e2);
            }
        }
        else
        {
            std::ofstream out(level_disk, std::ios::binary);
            out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Level>\n</Level>\n";
        }
    }

    lynx::Level::SetLinkedVoxelWorldOnDisk(level_disk.string(), legacy_voxels);

    std::ofstream save((assets / "save_file.txt"), std::ios::binary | std::ios::trunc);
    if (save)
        save << "world.level";

    std::cout << "[WORLD] Old level format converted : " << legacy_level << " + " << legacy_voxels
              << " -> world.level (startup level in assets/save_file.txt)\n";
    return "world.level";
}



// =============================================================================
// Editor
// =============================================================================















float camX;
float camY;
float camZ = 600.f;   // voxel units (1 = 1 voxel)

// Editor camera mode (buttons at the top right of the viewport) :
//   2D : looks straight at the level, WASD pans, the wheel changes the height ;
//   3D : free camera like Unreal : hold the right mouse button to look around,
//        WASD to fly, Q / E down / up, Shift faster, the wheel moves forward.
enum class EditorCameraMode { Mode2D = 0, Mode3D = 1 };
static EditorCameraMode cameraMode = EditorCameraMode::Mode2D;
static float camYaw = -90.f;   // degrees, -90 = looking into the level (2D view)
static float camPitch = 0.f;

bool dragging_object = false;

HRL_id editing_object;
HRL_id gizmo;

lynx::Actor* editing_actor = nullptr;


// Last gizmo values we know about (what the gizmo reported right after we
// synced it, or on the previous frame). A drag is detected by comparing the
// gizmo against these, NOT against the actor, because the gizmo may return
// slightly different floats than the ones it was given (euler conversions).
static float gizmoLastPos[3]   = { 0.f, 0.f, 0.f };
static float gizmoLastRot[3]   = { 0.f, 0.f, 0.f };
static float gizmoLastScale[3] = { 1.f, 1.f, 1.f };


static void ReadGizmo(float* pos, float* rot, float* scl)
{
    HRL_GetGizmoPosition(gizmo, &pos[0], &pos[1], &pos[2]);
    HRL_GetGizmoRotation(gizmo, &rot[0], &rot[1], &rot[2]);
    HRL_GetGizmoScale(gizmo, &scl[0], &scl[1], &scl[2]);
}


// Pushes a transform (location / rotation euler / scale) into the gizmo.
static void SyncGizmoFromTransform(const lynx::transform& t)
{
    HRL_SetGizmoPosition(gizmo, t.location.x, t.location.y, t.location.z);
    HRL_SetGizmoRotation(gizmo, t.rotation.x, t.rotation.y, t.rotation.z);
    HRL_SetGizmoScale(gizmo, t.scale.x, t.scale.y, t.scale.z);

    ReadGizmo(gizmoLastPos, gizmoLastRot, gizmoLastScale);
}


void SetObjectSelected(HRL_id object)
{
    if (object == HRL_INVALID_ID)
    {
        editing_actor = nullptr;

        HRL_SetGizmoVisible(
            gizmo,
            HRL_FALSE
        );
    }
    else
    {
        editing_actor =
            (lynx::Actor*)HRL_GetMeshUserHandle(object);

        if (!editing_actor)
        {
            printf("nullptr\n");
            return;
        }

        SyncGizmoFromTransform(editing_actor->transform);

        HRL_SetGizmoVisible(
            gizmo,
            HRL_TRUE
        );
    }
}


// Every selected actor (Ctrl / Shift click, rubber band in the Outliner).
// editing_actor is the primary one (Details, gizmo) and is always in it.
static std::vector<lynx::Actor*> selected_actors;

static bool IsActorAlive(lynx::Actor* actor);

// Dead actors (deleted, level rebuilt) leave the selection.
static void PruneActorSelection()
{
    std::erase_if(selected_actors, [](lynx::Actor* a) { return !IsActorAlive(a); });
    if (editing_actor && !IsActorAlive(editing_actor))
        editing_actor = nullptr;
    if (editing_actor && std::find(selected_actors.begin(), selected_actors.end(), editing_actor) == selected_actors.end())
        selected_actors.push_back(editing_actor);
}

static bool IsActorInSelection(lynx::Actor* actor)
{
    return actor && std::find(selected_actors.begin(), selected_actors.end(), actor) != selected_actors.end();
}

void SetActorSelected(lynx::Actor* actor);

// Ctrl + click : adds / removes. The primary actor becomes the last one added.
static void ToggleActorSelection(lynx::Actor* actor)
{
    if (!actor)
        return;
    auto it = std::find(selected_actors.begin(), selected_actors.end(), actor);
    if (it != selected_actors.end())
    {
        selected_actors.erase(it);
        lynx::Actor* primary = selected_actors.empty() ? nullptr : selected_actors.back();
        const std::vector<lynx::Actor*> keep = selected_actors;
        SetActorSelected(primary);
        selected_actors = keep;
        return;
    }
    const std::vector<lynx::Actor*> keep = selected_actors;
    SetActorSelected(actor);
    selected_actors = keep;
    selected_actors.push_back(actor);
}

// Several actors at once (Shift range, rubber band). `primary` : the gizmo one.
static void SetActorSelection(const std::vector<lynx::Actor*>& actors, lynx::Actor* primary)
{
    SetActorSelected(primary ? primary : (actors.empty() ? nullptr : actors.back()));
    selected_actors = actors;
    if (editing_actor && !IsActorInSelection(editing_actor))
        selected_actors.push_back(editing_actor);
}

void SetActorSelected(lynx::Actor* actor)
{
    editing_actor = actor;
    editing_object = HRL_INVALID_ID;

    // A plain selection : only this actor.
    selected_actors.clear();
    if (actor)
        selected_actors.push_back(actor);

    if (!editing_actor)
    {
        HRL_SetGizmoVisible(
            gizmo,
            HRL_FALSE
        );

        return;
    }

    SyncGizmoFromTransform(editing_actor->transform);

    HRL_SetGizmoVisible(
        gizmo,
        HRL_TRUE
    );
}


static void MarkEditorDirty();


// -----------------------------------------------------------------------------
// Editor undo history
// -----------------------------------------------------------------------------
// Keep the history at the editor level instead of tying it to one tool. This
// means Ctrl+Z follows the actual order in which the user edited the level.

using EditorPropertyValue = std::variant<
    int,
    float,
    bool,
    std::string,
    lynx::vec2,
    lynx::vec3,
    lynx::vec4,
    lynx::transform
>;

struct EditorActorSnapshot
{
    std::string type_name;
    std::string object_id;
    lynx::transform transform;
    std::unordered_map<std::string, EditorPropertyValue> properties;
};


static lynx::Level* editor_level = nullptr;

static bool IsActorAlive(lynx::Actor* actor)
{
    if (!actor || !editor_level)
        return false;
    const auto& actors = editor_level->GetActors();
    return std::find(actors.begin(), actors.end(), actor) != actors.end();
}

static lynx::Actor* outliner_anchor = nullptr;   // Shift + click range start
static std::vector<std::function<void()>> editorUndoHistory;


static void PushEditorUndo(std::function<void()> undo)
{
    if (!undo)
        return;

    editorUndoHistory.push_back(std::move(undo));
}


// Defined next to the brush code : puts the previewed voxels back.
static void ClearBrushPreview();


static void UndoLastEditorAction()
{
    if (editorUndoHistory.empty())
        return;

    ClearBrushPreview();

    std::function<void()> undo =
        std::move(editorUndoHistory.back());

    editorUndoHistory.pop_back();

    undo();
}


template<typename PropertyT>
static bool CaptureEditorProperty(
    const PropertyT& property,
    EditorPropertyValue& value)
{
    switch (property.GetType())
    {
        case 0:
        {
            if (auto ptr = std::get_if<int*>(&property.property_member);
                ptr && *ptr)
            {
                value = **ptr;
                return true;
            }
            break;
        }

        case 1:
        {
            if (auto ptr = std::get_if<float*>(&property.property_member);
                ptr && *ptr)
            {
                value = **ptr;
                return true;
            }
            break;
        }

        case 2:
        {
            if (auto ptr = std::get_if<bool*>(&property.property_member);
                ptr && *ptr)
            {
                value = **ptr;
                return true;
            }
            break;
        }

        case 3:
        {
            if (auto ptr = std::get_if<std::string*>(&property.property_member);
                ptr && *ptr)
            {
                value = **ptr;
                return true;
            }
            break;
        }

        case 4:
        {
            if (auto ptr = std::get_if<lynx::vec2*>(&property.property_member);
                ptr && *ptr)
            {
                value = **ptr;
                return true;
            }
            break;
        }

        case 5:
        {
            if (auto ptr = std::get_if<lynx::vec3*>(&property.property_member);
                ptr && *ptr)
            {
                value = **ptr;
                return true;
            }
            break;
        }

        case 6:
        {
            if (auto ptr = std::get_if<lynx::vec4*>(&property.property_member);
                ptr && *ptr)
            {
                value = **ptr;
                return true;
            }
            break;
        }

        case 7:
        {
            if (auto ptr = std::get_if<lynx::transform*>(&property.property_member);
                ptr && *ptr)
            {
                value = **ptr;
                return true;
            }
            break;
        }

        default:
            break;
    }

    return false;
}


template<typename PropertyT>
static bool ApplyEditorProperty(
    PropertyT& property,
    const EditorPropertyValue& value)
{
    switch (property.GetType())
    {
        case 0:
            if (auto ptr = std::get_if<int*>(&property.property_member);
                ptr && *ptr)
            {
                if (const auto* v = std::get_if<int>(&value))
                {
                    **ptr = *v;
                    return true;
                }
            }
            break;

        case 1:
            if (auto ptr = std::get_if<float*>(&property.property_member);
                ptr && *ptr)
            {
                if (const auto* v = std::get_if<float>(&value))
                {
                    **ptr = *v;
                    return true;
                }
            }
            break;

        case 2:
            if (auto ptr = std::get_if<bool*>(&property.property_member);
                ptr && *ptr)
            {
                if (const auto* v = std::get_if<bool>(&value))
                {
                    **ptr = *v;
                    return true;
                }
            }
            break;

        case 3:
            if (auto ptr = std::get_if<std::string*>(&property.property_member);
                ptr && *ptr)
            {
                if (const auto* v = std::get_if<std::string>(&value))
                {
                    **ptr = *v;
                    return true;
                }
            }
            break;

        case 4:
            if (auto ptr = std::get_if<lynx::vec2*>(&property.property_member);
                ptr && *ptr)
            {
                if (const auto* v = std::get_if<lynx::vec2>(&value))
                {
                    **ptr = *v;
                    return true;
                }
            }
            break;

        case 5:
            if (auto ptr = std::get_if<lynx::vec3*>(&property.property_member);
                ptr && *ptr)
            {
                if (const auto* v = std::get_if<lynx::vec3>(&value))
                {
                    **ptr = *v;
                    return true;
                }
            }
            break;

        case 6:
            if (auto ptr = std::get_if<lynx::vec4*>(&property.property_member);
                ptr && *ptr)
            {
                if (const auto* v = std::get_if<lynx::vec4>(&value))
                {
                    **ptr = *v;
                    return true;
                }
            }
            break;

        case 7:
            if (auto ptr = std::get_if<lynx::transform*>(&property.property_member);
                ptr && *ptr)
            {
                if (const auto* v = std::get_if<lynx::transform>(&value))
                {
                    **ptr = *v;
                    return true;
                }
            }
            break;

        default:
            break;
    }

    return false;
}


static EditorActorSnapshot CaptureActorSnapshot(
    lynx::Actor* actor)
{
    EditorActorSnapshot snapshot;

    if (!actor)
        return snapshot;

    snapshot.type_name = actor->GetTypeName();
    snapshot.object_id = actor->object_id_;
    snapshot.transform = actor->transform;

    for (const auto& [name, property] : actor->GetProperties())
    {
        if (name == "object_id_")
            continue;

        EditorPropertyValue value;

        if (CaptureEditorProperty(property, value))
            snapshot.properties.emplace(name, std::move(value));
    }

    return snapshot;
}


static lynx::Actor* RestoreActorSnapshot(
    lynx::Level* level,
    const EditorActorSnapshot& snapshot)
{
    if (!level || snapshot.type_name.empty())
        return nullptr;

    lynx::Actor* actor =
        level->SpawnActor(snapshot.type_name.c_str());

    if (!actor)
        return nullptr;

    actor->object_id_ = snapshot.object_id;

    auto& properties = actor->GetProperties();

    for (const auto& [name, value] : snapshot.properties)
    {
        auto it = properties.find(name);

        if (it != properties.end())
            ApplyEditorProperty(it->second, value);
    }

    // After the properties : "transform" is one of them, with the location of
    // the CAPTURE. snapshot.transform wins (actor.duplicate puts its offset
    // there ; before, the copy came back on top of the original).
    actor->transform = snapshot.transform;

    return actor;
}


static lynx::Actor* FindEditorActor(
    const std::string& object_id)
{
    if (!editor_level || object_id.empty())
        return nullptr;

    return editor_level->GetActorFromID(object_id.c_str());
}


// Gives a freshly spawned actor a unique, readable ID based on its class:
// "Tree", then "Tree_2", "Tree_3", ...
static void AssignDefaultActorName(
    lynx::Level* level,
    lynx::Actor* actor,
    const std::string& type_name)
{
    if (!level || !actor || type_name.empty())
        return;

    auto is_free = [&](const std::string& id)
    {
        lynx::Actor* existing =
            level->GetActorFromID(id.c_str());

        return !existing || existing == actor;
    };

    std::string id = type_name;
    int index = 2;

    while (!is_free(id))
        id = type_name + "_" + std::to_string(index++);

    actor->object_id_ = id;
}



// -----------------------------------------------------------------------------
// Place Actors thumbnails
// -----------------------------------------------------------------------------
// Prefer an image whose filename matches the registered actor type. This is
// intentionally filesystem-based so the editor does not have to construct a
// temporary actor just to discover its sprite/texture.
static std::unordered_map<std::string, HRL_id> actorPreviewTextures;
static std::unordered_set<std::string> actorPreviewSearchDone;

static bool IsPreviewImageExtension(
    const std::filesystem::path& path)
{
    std::string ext =
        path.extension().string();

    std::transform(
        ext.begin(),
        ext.end(),
        ext.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        }
    );

    return ext == ".png" ||
           ext == ".jpg" ||
           ext == ".jpeg" ||
           ext == ".bmp" ||
           ext == ".tga" ||
           ext == ".webp";
}



static bool IsColorPropertyName(const std::string& name)
{
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("color") != std::string::npos || lower.find("colour") != std::string::npos ||
           lower.find("tint") != std::string::npos;
}


// -----------------------------------------------------------------------------
// Place Actors drag/drop
// -----------------------------------------------------------------------------
// The dragged actor is a real actor instance. It is spawned when the drag
// starts, follows the mouse every frame, and is only committed to the level
// when the mouse is released over the scene. Cancelling the drag destroys it.
static lynx::Actor* actor_drag_preview = nullptr;
static std::string actor_drag_type;
static bool actor_drag_active = false;

static bool UpdateActorLocationFromMouse(lynx::Actor* actor)
{
    if (!actor)
        return false;

    double mouse_x = 0.0;
    double mouse_y = 0.0;

    lynx::editor::input::MousePosition(mouse_x, mouse_y);

    MouseToScene(
        mouse_x,
        mouse_y,
        mouse_x,
        mouse_y
    );

    int voxel_x = 0;
    int voxel_y = 0;

    if (!HRL_GetVoxelAtScreenPosition(
            scene,
            static_cast<int>(mouse_x),
            static_cast<int>(mouse_y),
            &voxel_x,
            &voxel_y))
    {
        return false;
    }

    float world_x = 0.f;
    float world_y = 0.f;

    if (!HRL_VoxelToWorldCoordinates(
            scene,
            static_cast<float>(voxel_x) + 0.5f,
            static_cast<float>(voxel_y) + 0.5f,
            &world_x,
            &world_y))
    {
        return false;
    }

    actor->transform.location.x = world_x;
    actor->transform.location.y = world_y;
    // Lights keep their height in front of the level (z), the rest goes on it.
    if (!dynamic_cast<lynx::LightActor*>(actor))
        actor->transform.location.z = 0.f;

    return true;
}

static void CancelActorPlacementDrag()
{
    if (actor_drag_preview && editor_level)
    {
        if (editing_actor == actor_drag_preview)
        {
            editing_actor = nullptr;
            editing_object = HRL_INVALID_ID;

            HRL_SetGizmoVisible(
                gizmo,
                HRL_FALSE
            );
        }

        editor_level->DestroyActor(
            actor_drag_preview
        );
    }

    actor_drag_preview = nullptr;
    actor_drag_type.clear();
    actor_drag_active = false;
}

static void BeginActorPlacementDrag(
    lynx::Level* level,
    const std::string& type_name)
{
    if (!level || type_name.empty())
        return;

    CancelActorPlacementDrag();

    actor_drag_preview =
        level->SpawnActor(
            type_name.c_str()
        );

    if (!actor_drag_preview)
        return;

    AssignDefaultActorName(
        level,
        actor_drag_preview,
        type_name
    );

    actor_drag_type = type_name;
    actor_drag_active = true;

    // Position it immediately under the cursor.
    UpdateActorLocationFromMouse(
        actor_drag_preview
    );

    SetActorSelected(
        actor_drag_preview
    );
}

static void CommitActorPlacementDrag()
{
    if (!actor_drag_preview ||
        !editor_level)
    {
        CancelActorPlacementDrag();
        return;
    }

    const std::string actor_id =
        actor_drag_preview->object_id_;

    PushEditorUndo([actor_id]()
    {
        if (lynx::Actor* created =
                FindEditorActor(actor_id))
        {
            if (editing_actor == created)
            {
                editing_actor = nullptr;
                editing_object = HRL_INVALID_ID;

                HRL_SetGizmoVisible(
                    gizmo,
                    HRL_FALSE
                );
            }

            editor_level->DestroyActor(
                created
            );
        }
    });

    SetActorSelected(
        actor_drag_preview
    );

    MarkEditorDirty();

    actor_drag_preview = nullptr;
    actor_drag_type.clear();
    actor_drag_active = false;
}





static bool editor_dirty = false;
static bool show_close_confirmation = false;
static bool show_copy_error = false;
static std::string copy_error_message;

static std::filesystem::path content_browser_root =
    std::filesystem::path("assets");

static std::filesystem::path content_browser_current_path =
    std::filesystem::path("assets");

// The Content Browser had the keyboard focus (last frame) : its Del / F2 /
// Ctrl+C... act on files, not on the selected actor.
static bool content_browser_focused = false;

struct PendingExplorerDrop
{
    std::vector<std::filesystem::path> paths;
    double mouse_x = 0.0;
    double mouse_y = 0.0;
};

static PendingExplorerDrop pending_explorer_drop;


static bool IsSupportedImageExtension(
    const std::filesystem::path& path)
{
    std::string extension = path.extension().string();

    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        }
    );

    return
        extension == ".png" ||
        extension == ".jpg" ||
        extension == ".jpeg";
}


static bool IsSupportedAudioExtension(
    const std::filesystem::path& path)
{
    std::string extension = path.extension().string();

    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        }
    );

    return
        extension == ".wav" ||
        extension == ".mp3";
}


static bool IsPathInside(
    const std::filesystem::path& path,
    const std::filesystem::path& root)
{
    std::error_code error;

    const std::filesystem::path canonical_root =
        std::filesystem::weakly_canonical(root, error);

    if (error)
        return false;

    const std::filesystem::path canonical_path =
        std::filesystem::weakly_canonical(path, error);

    if (error)
        return false;

    if (canonical_path == canonical_root)
        return true;

    const std::filesystem::path relative =
        std::filesystem::relative(
            canonical_path,
            canonical_root,
            error
        );

    if (error || relative.empty())
        return false;

    auto it = relative.begin();

    return it == relative.end() || *it != "..";
}


static bool GetAssetRelativePath(
    const std::filesystem::path& path,
    std::string& relative_path)
{
    if (!IsPathInside(path, content_browser_root))
        return false;

    std::error_code error;

    const std::filesystem::path canonical_root =
        std::filesystem::weakly_canonical(content_browser_root, error);

    if (error)
        return false;

    const std::filesystem::path canonical_path =
        std::filesystem::weakly_canonical(path, error);

    if (error ||
        !std::filesystem::is_regular_file(canonical_path, error) ||
        error)
    {
        return false;
    }

    const std::filesystem::path relative =
        std::filesystem::relative(
            canonical_path,
            canonical_root,
            error
        );

    if (error || relative.empty())
        return false;

    relative_path = relative.generic_string();
    return true;
}


static std::filesystem::path MakeUniqueDestinationPath(
    const std::filesystem::path& destination)
{
    if (!std::filesystem::exists(destination))
        return destination;

    const std::filesystem::path parent =
        destination.parent_path();

    const std::string stem =
        destination.stem().string();

    const std::string extension =
        destination.extension().string();

    for (int i = 1; ; ++i)
    {
        const std::filesystem::path candidate =
            parent /
            (stem + "_" + std::to_string(i) + extension);

        if (!std::filesystem::exists(candidate))
            return candidate;
    }
}


static void MarkEditorDirty()
{
    editor_dirty = true;
}


static void SaveEditor()
{
    if (!editor_level)
        return;

    // Previews of the plugins (sequencer...) put the actors back first.
    lynx::editor::plugins::BeforeLevelSnapshot();


    ClearBrushPreview();

    std::cout
        << "[SAVE] Saving voxel world to: "
        << world_file_path_string
        << "\n";

    HRL_SaveVoxelWorldAllFile(
        scene,
        world_file_path_string.c_str()
    );

    editor_level->SaveToFile(current_level_file.c_str());

    // Post process settings (Post Process window), with the level.
    if (lynx::postprocess::IsDirty())
        lynx::postprocess::Save();
    if (lynx::lighting2d::IsDirty())
        lynx::lighting2d::Save();

    editor_dirty = false;

    std::cout
        << "[SAVE] Done\n";
}


static void CopyExplorerFilesToContentBrowser()
{
    if (pending_explorer_drop.paths.empty())
        return;

    std::error_code error;

    if (!std::filesystem::exists(
            content_browser_current_path,
            error) ||
        !std::filesystem::is_directory(
            content_browser_current_path,
            error))
    {
        pending_explorer_drop.paths.clear();
        return;
    }

    for (const std::filesystem::path& source :
         pending_explorer_drop.paths)
    {
        error.clear();

        if (!std::filesystem::is_regular_file(source, error))
            continue;

        std::filesystem::path destination =
            content_browser_current_path /
            source.filename();

        destination =
            MakeUniqueDestinationPath(destination);

        error.clear();

        if (std::filesystem::copy_file(
                source,
                destination,
                std::filesystem::copy_options::none,
                error))
        {
            std::cout
                << "[CONTENT BROWSER] Copied: "
                << source.string()
                << " -> "
                << destination.string()
                << "\n";
        }
        else
        {
            copy_error_message =
                "Could not copy:\n" +
                source.string() +
                "\n\n" +
                error.message();

            show_copy_error = true;
        }
    }

    pending_explorer_drop.paths.clear();
}


// Spawns an actor of `actor_type` at the mouse position and stores the asset
// path (relative to assets/) in its string property `property_name`.
static void CreateActorFromAsset(
    const std::filesystem::path& asset_path,
    const char* actor_type,
    const char* property_name)
{
    if (!editor_level)
        return;

    std::string relative_path;

    if (!GetAssetRelativePath(asset_path, relative_path))
        return;

    lynx::Actor* actor =
        editor_level->SpawnActor(actor_type);

    if (!actor)
    {
        std::cerr
            << "[DRAG&DROP] Failed to create "
            << actor_type
            << "\n";
        return;
    }

    auto& properties =
        actor->GetProperties();

    auto property_it =
        properties.find(property_name);

    if (property_it == properties.end() ||
        property_it->second.GetType() != 3)
    {
        std::cerr
            << "[DRAG&DROP] "
            << actor_type
            << " has no string property '"
            << property_name
            << "'\n";

        editor_level->DestroyActor(actor);
        return;
    }

    auto string_ptr =
        std::get_if<std::string*>(
            &property_it->second.property_member
        );

    if (!string_ptr || !*string_ptr)
    {
        editor_level->DestroyActor(actor);
        return;
    }

    **string_ptr = relative_path;

    AssignDefaultActorName(editor_level, actor, actor_type);

    const std::string actor_id = actor->object_id_;

    PushEditorUndo([actor_id]()
    {
        if (lynx::Actor* created = FindEditorActor(actor_id))
        {
            if (editing_actor == created)
            {
                editing_actor = nullptr;
                editing_object = HRL_INVALID_ID;
                HRL_SetGizmoVisible(gizmo, HRL_FALSE);
            }

            editor_level->DestroyActor(created);
        }
    });

    double mouse_x = 0.0;
    double mouse_y = 0.0;

    lynx::editor::input::MousePosition(mouse_x, mouse_y);

    MouseToScene(mouse_x, mouse_y, mouse_x, mouse_y);

    int voxel_x;
    int voxel_y;

    if (HRL_GetVoxelAtScreenPosition(
            scene,
            static_cast<int>(mouse_x),
            static_cast<int>(mouse_y),
            &voxel_x,
            &voxel_y))
    {
        float world_x;
        float world_y;

        if (HRL_VoxelToWorldCoordinates(
                scene,
                static_cast<float>(voxel_x) + 0.5f,
                static_cast<float>(voxel_y) + 0.5f,
                &world_x,
                &world_y))
        {
            actor->transform.location.x = world_x;
            actor->transform.location.y = world_y;
            actor->transform.location.z = 0.f;
        }
    }

    SetActorSelected(actor);
    MarkEditorDirty();

    std::cout
        << "[DRAG&DROP] Created "
        << actor_type
        << ": "
        << relative_path
        << "\n";
}





// Script editors (one window per file) : ScriptEditors.h / .cpp.


// Right click menus, rename / duplicate / delete / new file... of the browser.
#include "ContentBrowserActions.inl"

// Git window (status, commit, pull / push, history) : Raven window windows/git.xml.
#include "GitWindow.inl"

// -----------------------------------------------------------------------------
// Outliner folders (like Unreal) : Actor::outliner_folder ("a/b"), and the
// list of the folders of the level (empty ones too) in Level::outliner_folders.
// -----------------------------------------------------------------------------

static std::string OutlinerParent(const std::string& path)
{
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

static std::string OutlinerName(const std::string& path)
{
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static bool OutlinerInside(const std::string& path, const std::string& folder)
{
    return path == folder || (path.size() > folder.size() && path.compare(0, folder.size(), folder) == 0 &&
                              path[folder.size()] == '/');
}

// Every folder : the declared ones, the ones of the actors, and their parents.
static std::set<std::string> OutlinerAllFolders(lynx::Level* level)
{
    std::set<std::string> all;
    auto add = [&](std::string f)
    {
        while (!f.empty())
        {
            all.insert(f);
            f = OutlinerParent(f);
        }
    };
    for (const std::string& f : level->outliner_folders)
        add(f);
    for (lynx::Actor* a : level->GetActors())
        if (a)
            add(a->outliner_folder);
    return all;
}

// Undo of any folder change : every folder of every actor, and the folder list.
static void PushOutlinerUndo(lynx::Level* level)
{
    std::vector<std::pair<lynx::Actor*, std::string>> actors;
    for (lynx::Actor* a : level->GetActors())
        if (a)
            actors.emplace_back(a, a->outliner_folder);
    const std::vector<std::string> folders = level->outliner_folders;
    PushEditorUndo([actors, folders]()
    {
        if (!editor_level)
            return;
        const auto& alive = editor_level->GetActors();
        for (const auto& [actor, folder] : actors)
            if (std::find(alive.begin(), alive.end(), actor) != alive.end())
                actor->outliner_folder = folder;
        editor_level->outliner_folders = folders;
        MarkEditorDirty();
    });
}

static void OutlinerDeclare(lynx::Level* level, const std::string& folder)
{
    if (!folder.empty() &&
        std::find(level->outliner_folders.begin(), level->outliner_folders.end(), folder) == level->outliner_folders.end())
        level->outliner_folders.push_back(folder);
}

// `from` (and everything inside) becomes `to`.
static void OutlinerRenameFolder(lynx::Level* level, const std::string& from, const std::string& to)
{
    if (from == to || to.empty() || OutlinerInside(to, from))
        return;
    auto moved = [&](const std::string& path) { return to + path.substr(from.size()); };
    for (lynx::Actor* a : level->GetActors())
        if (a && OutlinerInside(a->outliner_folder, from))
            a->outliner_folder = moved(a->outliner_folder);
    for (std::string& f : level->outliner_folders)
        if (OutlinerInside(f, from))
            f = moved(f);
    OutlinerDeclare(level, to);
}

// The folder goes ; what it holds goes to its parent.
static void OutlinerDeleteFolder(lynx::Level* level, const std::string& folder)
{
    const std::string parent = OutlinerParent(folder);
    auto lifted = [&](const std::string& path)
    {
        const std::string rest = path.substr(folder.size());   // "" or "/x/y"
        return parent.empty() ? (rest.empty() ? std::string() : rest.substr(1)) : parent + rest;
    };
    for (lynx::Actor* a : level->GetActors())
        if (a && OutlinerInside(a->outliner_folder, folder))
            a->outliner_folder = lifted(a->outliner_folder);
    std::vector<std::string> kept;
    for (const std::string& f : level->outliner_folders)
    {
        if (f == folder)
            continue;
        kept.push_back(OutlinerInside(f, folder) ? lifted(f) : f);
    }
    level->outliner_folders = kept;
}

static std::string OutlinerUniqueChild(lynx::Level* level, const std::string& parent, const std::string& base)
{
    const std::set<std::string> all = OutlinerAllFolders(level);
    std::string name = base;
    for (int i = 2; all.count(parent.empty() ? name : parent + "/" + name); ++i)
        name = base + " " + std::to_string(i);
    return parent.empty() ? name : parent + "/" + name;
}

static std::string outliner_rename_path;      // folder being renamed (popup)
static char outliner_rename_buffer[128] = {};
static bool outliner_rename_open = false;

static void OutlinerStartRename(const std::string& folder)
{
    outliner_rename_path = folder;
    std::snprintf(outliner_rename_buffer, sizeof(outliner_rename_buffer), "%s", OutlinerName(folder).c_str());
    outliner_rename_open = true;
}

static void OutlinerNewFolder(lynx::Level* level, const std::string& parent)
{
    PushOutlinerUndo(level);
    const std::string folder = OutlinerUniqueChild(level, parent, "New Folder");
    OutlinerDeclare(level, folder);
    MarkEditorDirty();
    OutlinerStartRename(folder);
}

static void OutlinerMoveActor(lynx::Level* level, lynx::Actor* actor, const std::string& folder)
{
    if (!actor || actor->outliner_folder == folder)
        return;
    PushOutlinerUndo(level);
    actor->outliner_folder = folder;
    OutlinerDeclare(level, folder);
    MarkEditorDirty();
}



// What the "Convert to voxels" dialog needs from the editor (undo, actors...).
static lynx::editor::sprite_voxels::Host SpriteVoxelHost()
{
    lynx::editor::sprite_voxels::Host host;
    host.scene = scene;
    host.push_undo = [](std::function<void()> undo) { PushEditorUndo(std::move(undo)); };
    host.mark_dirty = [] { MarkEditorDirty(); };
    host.find_actor = [](const std::string& id) { return FindEditorActor(id); };
    host.is_alive = [](lynx::Actor* actor)
    {
        if (!editor_level || !actor)
            return false;
        const auto& actors = editor_level->GetActors();
        return std::find(actors.begin(), actors.end(), actor) != actors.end();
    };
    host.save_voxel_types = [] { lynx::editor::voxel_types::Save(); };
    host.delete_actor = [](lynx::Actor* actor) -> std::function<void()>
    {
        if (!actor || !editor_level)
            return {};
        const EditorActorSnapshot snapshot = CaptureActorSnapshot(actor);
        if (editing_actor == actor)
        {
            editing_actor = nullptr;
            editing_object = HRL_INVALID_ID;
            HRL_SetGizmoVisible(gizmo, HRL_FALSE);
        }
        editor_level->DestroyActor(actor);
        return [snapshot]()
        {
            if (!editor_level || (!snapshot.object_id.empty() && FindEditorActor(snapshot.object_id)))
                return;
            if (lynx::Actor* restored = RestoreActorSnapshot(editor_level, snapshot))
                SetActorSelected(restored);
        };
    };
    return host;
}

static char content_browser_search[128] = {};

// Size of the Content Browser tiles (slider of the header, Ctrl + wheel in
// the grid). Remembered in editor_browser.txt.
static float content_browser_thumb_scale = 1.f;
static bool content_browser_thumb_loaded = false;

static void LoadContentBrowserThumbScale()
{
    content_browser_thumb_loaded = true;
    std::ifstream in("editor_browser.txt");
    float scale = 1.f;
    if (in >> scale && scale >= 0.5f && scale <= 2.5f)
        content_browser_thumb_scale = scale;
}

static void SaveContentBrowserThumbScale()
{
    std::ofstream out("editor_browser.txt", std::ios::trunc);
    if (out)
        out << content_browser_thumb_scale << "\n";
}
static int content_browser_item_count = 0;

// Navigation history (like a web browser) : the back / forward buttons of the
// mouse, and the arrows of the header.
static std::vector<std::filesystem::path> content_browser_back;
static std::vector<std::filesystem::path> content_browser_forward;
static std::filesystem::path content_browser_last_path;

// Called once per frame : a folder change made anywhere (click, breadcrumb,
// up, rename...) becomes a step of the history.
static void ContentBrowserTrackHistory()
{
    if (content_browser_current_path == content_browser_last_path)
        return;
    if (!content_browser_last_path.empty())
    {
        content_browser_back.push_back(content_browser_last_path);
        if (content_browser_back.size() > 100)
            content_browser_back.erase(content_browser_back.begin());
        content_browser_forward.clear();
    }
    content_browser_last_path = content_browser_current_path;
}

// -1 : back, +1 : forward. Folders deleted since are skipped.
static void ContentBrowserGo(int direction)
{
    std::vector<std::filesystem::path>& from = direction < 0 ? content_browser_back : content_browser_forward;
    std::vector<std::filesystem::path>& to = direction < 0 ? content_browser_forward : content_browser_back;
    while (!from.empty())
    {
        std::filesystem::path target = from.back();
        from.pop_back();
        std::error_code error;
        const bool inside = target == content_browser_root || IsPathInside(target, content_browser_root);
        if (!inside || !std::filesystem::is_directory(target, error))
            continue;
        to.push_back(content_browser_current_path);
        content_browser_current_path = target;
        content_browser_last_path = target;     // not a new step
        return;
    }
}


// One cell of the Content Browser grid (drawn over its invisible button).
// -----------------------------------------------------------------------------
// Folder listing off the main thread. Listing a folder means one disk access
// per file (type of the entry) : with hundreds of files it costs milliseconds,
// a hitch every refresh. The content of a NEW folder is listed at once (the
// grid must never show the files of the previous folder) ; the periodic
// refreshes run on a worker thread and the result is swapped in when ready.
// -----------------------------------------------------------------------------

static std::vector<std::filesystem::directory_entry> ListFolderSorted(const std::filesystem::path& folder)
{
    std::vector<std::filesystem::directory_entry> entries;
    std::error_code error;

    if (!std::filesystem::is_directory(folder, error))
        return entries;

    for (const auto& entry :
         std::filesystem::directory_iterator(
             folder,
             std::filesystem::directory_options::skip_permission_denied,
             error))
    {
        if (error)
            break;
        entries.push_back(entry);
    }

    // Folders first, then by name. The type is read once per entry here.
    std::vector<std::pair<bool, std::string>> keys;
    keys.reserve(entries.size());
    std::vector<size_t> order(entries.size());
    for (size_t i = 0; i < entries.size(); ++i)
    {
        std::error_code dir_error;
        keys.emplace_back(entries[i].is_directory(dir_error), entries[i].path().filename().string());
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&keys](size_t a, size_t b)
    {
        if (keys[a].first != keys[b].first)
            return keys[a].first > keys[b].first;
        return keys[a].second < keys[b].second;
    });

    std::vector<std::filesystem::directory_entry> sorted;
    sorted.reserve(entries.size());
    for (size_t i : order)
        sorted.push_back(std::move(entries[i]));
    return sorted;
}

class AsyncFolderList
{
public:
    /**
     * Entries of `folder` (folders first, then by name). Listed right away
     * when the folder changes or `now` is true ; otherwise refreshed in the
     * background every `refresh_seconds` (the previous list is shown
     * meanwhile). `time` : ImGui::GetTime().
     */
    const std::vector<std::filesystem::directory_entry>& Get(
        const std::filesystem::path& folder, double time, double refresh_seconds, bool now = false)
    {
        // A background listing finished : taken if it is still the right folder.
        if (job_.IsValid() && job_.IsDone())
        {
            job_.Reset();
            if (pending_ && pending_->folder == folder_)
                entries_ = std::move(pending_->entries);
            pending_.reset();
        }

        if (folder != folder_ || now || listed_time_ < 0.0)
        {
            folder_ = folder;
            entries_ = ListFolderSorted(folder);
            listed_time_ = time;
            // A refresh still running lists the old state : ignored.
            pending_.reset();
            job_.Reset();
        }
        else if (time - listed_time_ > refresh_seconds && !job_.IsValid())
        {
            listed_time_ = time;
            auto result = std::make_shared<Result>();
            result->folder = folder_;
            pending_ = result;
            job_ = lynx::jobs::Submit([result]()
            {
                result->entries = ListFolderSorted(result->folder);
            });
        }
        return entries_;
    }

private:
    struct Result
    {
        std::filesystem::path folder;
        std::vector<std::filesystem::directory_entry> entries;
    };

    std::filesystem::path folder_;
    std::vector<std::filesystem::directory_entry> entries_;
    double listed_time_ = -1.0;
    lynx::jobs::Handle job_;
    std::shared_ptr<Result> pending_;
};











// -----------------------------------------------------------------------------
// Editor state (everything that used to be local to main())
// -----------------------------------------------------------------------------

static HRL_id editor_camera = HRL_INVALID_ID;
static HRL_id editor_viewport = HRL_INVALID_ID;
static std::string editor_save_data;

static float cameraSpeed = 50.f;

// -----------------------------------------------------------------------------
// Application settings
// -----------------------------------------------------------------------------
// Stored in the project folder (working directory) so the editor keeps the same preferences
// between launches. The file is deliberately simple and human-readable.
static const char* const kSettingsFile = "settings.cfg";

struct AppSettings
{
    bool vsync = false;
    float masterVolume = 0.5f;
    float cameraSpeed = 50.f;
    HRL_EVoxelRenderMode voxelRenderMode = HRL_VOXEL_BLOCKY;
    // Units : 1 world unit = 1 voxel, always (HRL_SetVoxelPhysicalSize 1).
    // Projects made before ("physical_voxel_size" in settings.cfg, 0.3 by
    // default, no "units voxel") are converted once : MigrateToVoxelUnits().
    float legacyVoxelSize = 0.3f;
    bool voxelUnits = false;

    // Hot reload of the game DLL.
    bool reloadAfterBuild = true;     // "Compile" -> reload when it succeeded
    bool autoReloadOnChange = true;   // build/<game>.dll rebuilt outside the editor

    // Renderer of the game (LynxRuntime, shipped game) : Vulkan or OpenGL.
    // The editor Viewport itself stays rendered by OpenGL (GlBridge).
    lynx::host::RenderBackend renderer = lynx::host::kDefaultRenderBackend;
};

static AppSettings appSettings;

static void SaveSettings()
{
    std::ofstream file(kSettingsFile, std::ios::trunc);

    if (!file)
    {
        std::cerr
            << "[SETTINGS] Could not write "
            << kSettingsFile
            << "\n";
        return;
    }

    file << "version 2\n";
    file << "units voxel\n";
    file << "vsync " << (appSettings.vsync ? 1 : 0) << "\n";
    file << std::setprecision(9);
    file << "master_volume " << appSettings.masterVolume << "\n";
    file << "camera_speed " << appSettings.cameraSpeed << "\n";
    file << "voxel_render_mode "
         << static_cast<int>(appSettings.voxelRenderMode)
         << "\n";
    file << "reload_after_build " << (appSettings.reloadAfterBuild ? 1 : 0) << "\n";
    file << "auto_reload_on_change " << (appSettings.autoReloadOnChange ? 1 : 0) << "\n";
    file << "renderer " << lynx::host::RenderBackendKey(appSettings.renderer) << "\n";
}

static void LoadSettings()
{
    std::ifstream file(kSettingsFile);

    if (!file)
        return;

    std::string key;

    while (file >> key)
    {
        if (key == "version")
        {
            int version = 0;
            file >> version;
        }
        else if (key == "vsync")
        {
            int value = 0;
            if (file >> value)
                appSettings.vsync = value != 0;
        }
        else if (key == "master_volume")
        {
            float value = appSettings.masterVolume;
            if (file >> value && std::isfinite(value))
                appSettings.masterVolume = std::clamp(value, 0.f, 2.f);
        }
        else if (key == "camera_speed")
        {
            float value = appSettings.cameraSpeed;
            if (file >> value && std::isfinite(value))
                appSettings.cameraSpeed = std::clamp(value, 1.f, 400.f);
        }
        else if (key == "voxel_render_mode")
        {
            int value = static_cast<int>(appSettings.voxelRenderMode);
            if (file >> value)
            {
                value = std::clamp(
                    value,
                    static_cast<int>(HRL_VOXEL_FLAT),
                    static_cast<int>(HRL_VOXEL_BLOCKY)
                );

                appSettings.voxelRenderMode =
                    static_cast<HRL_EVoxelRenderMode>(value);
            }
        }
        else if (key == "physical_voxel_size")
        {
            float value = appSettings.legacyVoxelSize;
            if (file >> value && std::isfinite(value))
                appSettings.legacyVoxelSize =
                    std::clamp(value, 0.001f, 100.f);
        }
        else if (key == "units")
        {
            std::string value;
            if (file >> value)
                appSettings.voxelUnits = value == "voxel";
        }
        else if (key == "reload_after_build")
        {
            int value = 1;
            if (file >> value)
                appSettings.reloadAfterBuild = value != 0;
        }
        else if (key == "auto_reload_on_change")
        {
            int value = 1;
            if (file >> value)
                appSettings.autoReloadOnChange = value != 0;
        }
        else if (key == "renderer")
        {
            std::string value;
            if (file >> value)
                appSettings.renderer = lynx::host::ParseRenderBackend(value);
        }
        else
        {
            // Ignore unknown settings so newer versions remain backwards
            // compatible with older settings files.
            std::string ignored;
            file >> ignored;
        }
    }
}

static void ApplyRuntimeSettings()
{
    Rset_vsync(appSettings.vsync ? R_TRUE : R_FALSE);

    cameraSpeed = appSettings.cameraSpeed;

    lynx::SetMasterVolume(
        appSettings.masterVolume
    );

    if (HRL_IsValidScene(scene))
    {
        HRL_SetVoxelRenderMode(
            scene,
            appSettings.voxelRenderMode
        );
        HRL_SetVoxelPhysicalSize(scene, 1.f);   // 1 world unit = 1 voxel
    }
}

static const char* VoxelRenderModeName(HRL_EVoxelRenderMode mode)
{
    switch (mode)
    {
        case HRL_VOXEL_FLAT:
            return "Flat";

        case HRL_VOXEL_SMOOTH:
            return "Smooth";

        case HRL_VOXEL_BLOCKY:
            return "Blocky";

        default:
            return "Unknown";
    }
}


// Editor camera height limits and speed scaling.
// The "Camera Speed" slider is the speed at kCameraSpeedRefZ ; the real speed
// is proportional to the camera height (far = faster, close = slower).
// Voxel units (1 = 1 voxel) : the editor sees from ~20 to ~1000 voxels across.
constexpr float kCameraMinZ = 20.f;
constexpr float kCameraMaxZ = 1000.f;
constexpr float kCameraSpeedRefZ = 200.f;

static float ClampCameraZ(float z)
{
    return std::clamp(z, kCameraMinZ, kCameraMaxZ);
}

static float GetCameraSpeedForHeight()
{
    return cameraSpeed * (camZ / kCameraSpeedRefZ);
}
static int brushRadius = 3;
static int brushVoxelType = 4;

// Available brush shapes. Radius is measured in voxels from the center.
enum class BrushShape
{
    Single = 0,
    Circle,
    Square,
    Diamond,
    Horizontal,
    Vertical,
    Cross,
    Line,
    Rectangle,
    RectangleOutline,
    CircleOutline
};

static BrushShape brushShape = BrushShape::Circle;

static bool IsDragShape(BrushShape shape)
{
    return shape == BrushShape::Line ||
           shape == BrushShape::Rectangle ||
           shape == BrushShape::RectangleOutline ||
           shape == BrushShape::CircleOutline;
}

static int GetBrushIterationRadius()
{
    return brushShape == BrushShape::Single ? 0 : brushRadius;
}

static bool BrushContains(int x, int y)
{
    switch (brushShape)
    {
        case BrushShape::Single:
            return x == 0 && y == 0;

        case BrushShape::Square:
            return true;

        case BrushShape::Diamond:
            return std::abs(x) + std::abs(y) <= brushRadius;

        case BrushShape::Horizontal:
            return y == 0;

        case BrushShape::Vertical:
            return x == 0;

        case BrushShape::Cross:
            return x == 0 || y == 0;

        case BrushShape::Circle:
        default:
            return x * x + y * y <= brushRadius * brushRadius;
    }
}

// Type actually used by the brush this frame : brushVoxelType, or 0 (empty)
// while Shift is held in the editor. Refreshed every frame in Tick().
static int brushActiveType = 4;
static float brushDensity = 1.f;
static bool brushPaintEnabled = false;
static bool brushPaintEmptyVoxels = true;
static std::unordered_set<unsigned long long> brushPaintedVoxels;
static bool brushPainting = false;

static bool gizmoUndoActive = false;
static std::string gizmoUndoActorId;
static lynx::transform gizmoUndoTransform{};
// The other selected actors moved with the gizmo : their transform before.
static std::vector<std::pair<std::string, lynx::transform>> gizmoUndoOthers;

struct VoxelChange
{
    int x;
    int y;
    int previousType;
    int newType;
};

static std::vector<VoxelChange> currentStrokeChanges;
static std::mt19937 brushRandomGenerator(std::random_device{}());
static std::uniform_real_distribution<float> brushRandomDistribution(0.f, 1.f);

static bool f4_was_down = false;
static bool f3_was_down = false;
static bool ctrl_s_was_down = false;


static unsigned long long brushVoxelKey(int x, int y)
{
    return
        (static_cast<unsigned long long>(
            static_cast<unsigned int>(x)
        ) << 32) |
        static_cast<unsigned int>(y);
}


static void tryPaintVoxel(int x, int y)
{
    const unsigned long long key =
        brushVoxelKey(x, y);

    if (!brushPaintedVoxels.insert(key).second)
        return;

    if (brushDensity <= 0.f)
        return;

    if (brushDensity < 1.f &&
        brushRandomDistribution(
            brushRandomGenerator
        ) >= brushDensity)
    {
        return;
    }

    const int previousType =
        HRL_GetVoxelType(
            scene,
            x,
            y
        );

    if (!brushPaintEmptyVoxels &&
        previousType == 0)
    {
        return;
    }

    if (previousType == brushActiveType)
        return;

    currentStrokeChanges.push_back({
        x,
        y,
        previousType,
        brushActiveType
    });

    // Simulate : the voxel physics keeps a copy of the voxels.
    lynx::voxel_physics::InvalidateAllVoxels();

    HRL_SetVoxelType(
        scene,
        x,
        y,
        brushActiveType
    );

    MarkEditorDirty();
}


// ---------------------------------------------------------------------------
// Brush preview (paints the voxels for real, then restores them)
// ---------------------------------------------------------------------------

struct BrushPreviewVoxel
{
    int x;
    int y;
    int previousType;
};

static std::vector<BrushPreviewVoxel> brushPreviewVoxels;

// Puts every previewed voxel back to its real type.
// Does NOT mark the editor dirty (a preview is not a modification).
static void ClearBrushPreview()
{
    if (!brushPreviewVoxels.empty())
        lynx::voxel_physics::InvalidateAllVoxels();   // Simulate : physics voxel cache
    for (auto it = brushPreviewVoxels.rbegin();
         it != brushPreviewVoxels.rend();
         ++it)
    {
        HRL_SetVoxelType(
            scene,
            it->x,
            it->y,
            it->previousType
        );
    }

    brushPreviewVoxels.clear();
}

// Shows what a click would paint at (centerX, centerY).
// Density is ignored on purpose (it is random, it would flicker).
static void ApplyBrushPreview(int centerX, int centerY)
{
    ClearBrushPreview();

    const int iterationRadius = GetBrushIterationRadius();

    for (int y = -iterationRadius; y <= iterationRadius; ++y)
    {
        for (int x = -iterationRadius; x <= iterationRadius; ++x)
        {
            if (!BrushContains(x, y))
                continue;

            const int vx = centerX + x;
            const int vy = centerY + y;

            const int previousType =
                HRL_GetVoxelType(scene, vx, vy);

            if (!brushPaintEmptyVoxels && previousType == 0)
                continue;

            if (previousType == brushActiveType)
                continue;

            brushPreviewVoxels.push_back({ vx, vy, previousType });

            HRL_SetVoxelType(scene, vx, vy, brushActiveType);
        }
    }
}

// Preview for drag-based tools. Like ApplyBrushPreview(), this temporarily
// writes voxels and remembers their previous values so the preview can be
// removed without creating an editor modification.
static void PreviewStampBrush(int centerX, int centerY)
{
    lynx::voxel_physics::InvalidateAllVoxels();   // Simulate : physics voxel cache
    const int iterationRadius = GetBrushIterationRadius();

    for (int y = -iterationRadius; y <= iterationRadius; ++y)
    {
        for (int x = -iterationRadius; x <= iterationRadius; ++x)
        {
            if (!BrushContains(x, y))
                continue;

            const int vx = centerX + x;
            const int vy = centerY + y;
            const int previousType = HRL_GetVoxelType(scene, vx, vy);

            if (!brushPaintEmptyVoxels && previousType == 0)
                continue;

            if (previousType == brushActiveType)
                continue;

            brushPreviewVoxels.push_back({ vx, vy, previousType });
            HRL_SetVoxelType(scene, vx, vy, brushActiveType);
        }
    }
}

static void ApplyDragBrushPreview(int x0, int y0, int x1, int y1)
{
    ClearBrushPreview();

    auto previewLine = [&](int ax, int ay, int bx, int by)
    {
        const int dx = std::abs(bx - ax);
        const int dy = -std::abs(by - ay);
        const int sx = (ax < bx) ? 1 : -1;
        const int sy = (ay < by) ? 1 : -1;
        int err = dx + dy;

        while (true)
        {
            PreviewStampBrush(ax, ay);

            if (ax == bx && ay == by)
                break;

            const int e2 = 2 * err;
            if (e2 >= dy)
            {
                err += dy;
                ax += sx;
            }
            if (e2 <= dx)
            {
                err += dx;
                ay += sy;
            }
        }
    };

    switch (brushShape)
    {
        case BrushShape::Line:
            previewLine(x0, y0, x1, y1);
            break;

        case BrushShape::Rectangle:
        case BrushShape::RectangleOutline:
        {
            const int minX = std::min(x0, x1);
            const int maxX = std::max(x0, x1);
            const int minY = std::min(y0, y1);
            const int maxY = std::max(y0, y1);
            const bool outline = brushShape == BrushShape::RectangleOutline;

            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    if (outline &&
                        x != minX && x != maxX &&
                        y != minY && y != maxY)
                        continue;

                    PreviewStampBrush(x, y);
                }
            }
            break;
        }

        case BrushShape::CircleOutline:
        {
            const float cx = (x0 + x1) * 0.5f;
            const float cy = (y0 + y1) * 0.5f;
            const float rx = std::max(0.5f, std::abs(x1 - x0) * 0.5f);
            const float ry = std::max(0.5f, std::abs(y1 - y0) * 0.5f);
            const int minX = static_cast<int>(std::floor(cx - rx)) - brushRadius;
            const int maxX = static_cast<int>(std::ceil(cx + rx)) + brushRadius;
            const int minY = static_cast<int>(std::floor(cy - ry)) - brushRadius;
            const int maxY = static_cast<int>(std::ceil(cy + ry)) + brushRadius;
            const float innerX = std::max(0.0f, rx - 1.0f);
            const float innerY = std::max(0.0f, ry - 1.0f);

            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    const float nx = (x - cx) / rx;
                    const float ny = (y - cy) / ry;
                    const float outer = nx * nx + ny * ny;
                    bool insideInner = false;

                    if (innerX > 0.f && innerY > 0.f)
                    {
                        const float ix = (x - cx) / innerX;
                        const float iy = (y - cy) / innerY;
                        insideInner = ix * ix + iy * iy < 1.f;
                    }

                    if (outer <= 1.25f && !insideInner)
                        PreviewStampBrush(x, y);
                }
            }
            break;
        }

        default:
            break;
    }
}


// ---------------------------------------------------------------------------
// Stroke interpolation : when the mouse moves faster than one brush radius
// per frame, the brush is stamped on every voxel of the line between the
// previous and the current position, so the stroke has no gaps.
// ---------------------------------------------------------------------------

static bool brushHasLastCenter = false;
static int brushLastCenterX = 0;
static int brushLastCenterY = 0;

// Start/end points used by drag-based tools (Line, Rectangle, ...).
static bool brushDragHasStart = false;
static int brushDragStartX = 0;
static int brushDragStartY = 0;

static void stampBrush(int centerX, int centerY);

static void paintBrushLine(int x0, int y0, int x1, int y1)
{
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = (x0 < x1) ? 1 : -1;
    const int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true)
    {
        stampBrush(x0, y0);

        if (x0 == x1 && y0 == y1)
            break;

        const int e2 = 2 * err;

        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }

        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

static void paintBrushRectangle(int x0, int y0, int x1, int y1, bool outline)
{
    const int minX = std::min(x0, x1);
    const int maxX = std::max(x0, x1);
    const int minY = std::min(y0, y1);
    const int maxY = std::max(y0, y1);

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            if (outline &&
                x != minX && x != maxX &&
                y != minY && y != maxY)
                continue;

            stampBrush(x, y);
        }
    }
}

static void paintBrushCircleOutline(int x0, int y0, int x1, int y1)
{
    const float cx = (x0 + x1) * 0.5f;
    const float cy = (y0 + y1) * 0.5f;
    const float rx = std::max(0.5f, std::abs(x1 - x0) * 0.5f);
    const float ry = std::max(0.5f, std::abs(y1 - y0) * 0.5f);

    const int minX = static_cast<int>(std::floor(cx - rx)) - brushRadius;
    const int maxX = static_cast<int>(std::ceil (cx + rx)) + brushRadius;
    const int minY = static_cast<int>(std::floor(cy - ry)) - brushRadius;
    const int maxY = static_cast<int>(std::ceil (cy + ry)) + brushRadius;

    const float innerX = std::max(0.0f, rx - 1.0f);
    const float innerY = std::max(0.0f, ry - 1.0f);

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            const float nx = (x - cx) / rx;
            const float ny = (y - cy) / ry;
            const float outer = nx * nx + ny * ny;

            bool insideInner = false;
            if (innerX > 0.f && innerY > 0.f)
            {
                const float ix = (x - cx) / innerX;
                const float iy = (y - cy) / innerY;
                insideInner = ix * ix + iy * iy < 1.f;
            }

            if (outer <= 1.25f && !insideInner)
                stampBrush(x, y);
        }
    }
}

static void stampDragShape(int x0, int y0, int x1, int y1)
{
    switch (brushShape)
    {
        case BrushShape::Line:
            paintBrushLine(x0, y0, x1, y1);
            break;

        case BrushShape::Rectangle:
            paintBrushRectangle(x0, y0, x1, y1, false);
            break;

        case BrushShape::RectangleOutline:
            paintBrushRectangle(x0, y0, x1, y1, true);
            break;

        case BrushShape::CircleOutline:
            paintBrushCircleOutline(x0, y0, x1, y1);
            break;

        default:
            break;
    }
}

static void stampBrush(int centerX, int centerY)
{
    const int iterationRadius = GetBrushIterationRadius();

    for (int y = -iterationRadius; y <= iterationRadius; ++y)
    {
        for (int x = -iterationRadius; x <= iterationRadius; ++x)
        {
            if (BrushContains(x, y))
                tryPaintVoxel(centerX + x, centerY + y);
        }
    }
}

// Bresenham line from the last stamped center to (cx, cy), both included.
// tryPaintVoxel already ignores voxels visited during the current stroke,
// so overlapping stamps only cost a hash lookup.
static void paintBrushStroke(int cx, int cy)
{
    if (!brushHasLastCenter)
    {
        stampBrush(cx, cy);
    }
    else
    {
        int x = brushLastCenterX;
        int y = brushLastCenterY;

        const int dx = std::abs(cx - x);
        const int dy = -std::abs(cy - y);
        const int sx = (x < cx) ? 1 : -1;
        const int sy = (y < cy) ? 1 : -1;

        int err = dx + dy;

        while (true)
        {
            stampBrush(x, y);

            if (x == cx && y == cy)
                break;

            const int e2 = 2 * err;

            if (e2 >= dy)
            {
                err += dy;
                x += sx;
            }

            if (e2 <= dx)
            {
                err += dx;
                y += sy;
            }
        }
    }

    brushHasLastCenter = true;
    brushLastCenterX = cx;
    brushLastCenterY = cy;
}


static lynx::Engine* editor_engine = nullptr;
static HRL_id editor_gameplay_cam = HRL_INVALID_ID;


// 0 = translate, 1 = rotate, 2 = scale
static int gizmoModeIndex = 0;

static void SetGizmoModeIndex(int index)
{
    gizmoModeIndex = ((index % 3) + 3) % 3;

    static const char* names[] = { "TRANSLATE", "ROTATE", "SCALE" };
    std::cout << "[GIZMO] Mode: " << names[gizmoModeIndex] << "\n";

    switch (gizmoModeIndex)
    {
    case 0:
        HRL_SetGizmoMode(gizmo, HRL_GIZMO_MODE_TRANSLATE);
        break;

    case 1:
        HRL_SetGizmoMode(gizmo, HRL_GIZMO_MODE_ROTATE);
        break;

    default:
        HRL_SetGizmoMode(gizmo, HRL_GIZMO_MODE_SCALE);
        break;
    }
}


// Space: translate -> rotate -> scale -> translate -> ...
static void CycleGizmoMode()
{
    SetGizmoModeIndex(gizmoModeIndex + 1);
}


// -----------------------------------------------------------------------------
// Play snapshot : the state of the editor right before pressing Play (actors
// + voxel world). When the game stops, main() restores it between two frames.
// -----------------------------------------------------------------------------

static const char* const kPlayBackupLevelFile = "play_backup.xml";   // working dir (same as world.xml)
static const char* const kPlayBackupVoxelName = "play_backup.vox";   // relative to assets/

static bool play_snapshot_valid = false;
static bool restore_after_play_requested = false;

// The voxel world of the snapshot, in memory (compressed .hrlv) : Stop puts
// it back from here, without reading the disk. The file is still written (on
// a worker thread, Play does not wait for the disk).
static std::shared_ptr<std::vector<uint8_t>> play_voxel_snapshot;
static lynx::jobs::Handle play_voxel_snapshot_write;


static void SavePlaySnapshot()
{
    play_snapshot_valid = false;

    ClearBrushPreview();

    if (!editor_level)
        return;

    // Encoded by HRL here (main thread : HRL), written by a worker.
    play_voxel_snapshot_write.Wait();   // the previous write (same file)
    auto snapshot = std::make_shared<std::vector<uint8_t>>();
    if (const size_t size = HRL_GetVoxelWorldSaveAllSize(scene); size > 0)
    {
        snapshot->resize(size);
        snapshot->resize(HRL_SaveVoxelWorldAll(scene, snapshot->data(), snapshot->size()));
    }
    play_voxel_snapshot = snapshot;

    if (!snapshot->empty())
    {
        std::error_code error;
        const std::filesystem::path voxel_path =
            std::filesystem::absolute(std::filesystem::path("assets") / kPlayBackupVoxelName, error);
        play_voxel_snapshot_write = lynx::jobs::Submit([snapshot, voxel_path]()
        {
            std::ofstream out(voxel_path, std::ios::binary | std::ios::trunc);
            if (out)
                out.write(reinterpret_cast<const char*>(snapshot->data()),
                          static_cast<std::streamsize>(snapshot->size()));
            if (!out)
                std::cerr << "[PLAY] could not write " << voxel_path.string() << "\n";
        });
    }

    editor_level->SaveToFile(kPlayBackupLevelFile);

    play_snapshot_valid = true;

    std::cout << "[PLAY] Snapshot saved\n";
}


// Editor <-> Game. Used by the F3 shortcut and by the toolbar Play/Stop button.
// `simulate` (Simulate button, Alt+S) : the game runs without possessing any
// actor, the editor camera and its movements stay (see isSimulating). While
// the game runs, any call stops it (Play or Simulate).
static void TogglePlayMode(bool simulate = false)
{
    if (!editor_engine)
        return;


    if (!isPlaying)
    {
        // EDITOR -> GAME
        // Previews of the plugins put the actors back first.
        lynx::editor::plugins::BeforeLevelSnapshot();

        // Remember the current state so Stop can put everything back.
        SavePlaySnapshot();

        // Clear editor selection and hide the gizmo in game mode.
        editing_object = HRL_INVALID_ID;
        editing_actor = nullptr;
        selected_actors.clear();

        HRL_SetGizmoVisible(
            gizmo,
            HRL_FALSE
        );

        // Before StartGame : no auto possess, the default player keeps the
        // editor camera whatever the CameraComponents ask.
        isSimulating = simulate;
        editor_engine->SetSimulating(simulate, editor_camera);

        SetPlaying(
            editor_engine,
            editor_viewport,
            simulate ? editor_camera : editor_gameplay_cam,
            true
        );

        std::cout << (simulate ? "[PLAY] Switched to SIMULATE mode\n" : "[PLAY] Switched to GAME mode\n");
    }
    else
    {
        // GAME -> EDITOR
        SetPlaying(editor_engine, editor_viewport, editor_camera, false);

        isSimulating = false;
        editor_engine->SetSimulating(false);

        // The level is rebuilt by main() between two frames (the panels still
        // hold actor pointers during the current ImGui frame).
        if (play_snapshot_valid)
            restore_after_play_requested = true;

        std::cout << "[PLAY] Switched to EDITOR mode\n";
    }
}


// Raised by the toolbar "Reload Game" button, consumed by main() through
// editor::ConsumeReloadRequest().
static bool reload_game_requested = false;

// Hot reload wanted (build finished / DLL changed on disk) while playing :
// done at the next Stop, the game is not interrupted.
static bool hot_reload_pending = false;

// build/<game>.dll changed but auto reload is off : shown in the toolbar.
static bool module_changed_notice = false;

static void RequestHotReload()
{
    if (isPlaying)
        hot_reload_pending = true;
    else
        reload_game_requested = true;
}

// Raised by the toolbar "Project > Open another project...". The editor closes
// (through the usual "Save before quitting?" dialog) and main() starts a new
// editor process on the project browser.
static bool switch_project_requested = false;

// Shown once in a popup (ex : game DLL older than the engine).
static std::string editor_warning;
static bool editor_warning_pending = false;

static void ShowEditorWarning(const std::string& text)
{
    if (text.empty())
        return;

    std::cerr << "[WARNING] " << text << "\n";

    // Several warnings before the popup opens : all of them are shown.
    if (editor_warning_pending && !editor_warning.empty())
        editor_warning += "\n\n" + text;
    else
        editor_warning = text;

    editor_warning_pending = true;
}


// Commands for Python scripts / AI tools (actor.spawn, voxel.fill, asset.write...).
#include "EditorCommands.inl"



// Top bar. Every button has a keyboard shortcut that keeps working.
// -----------------------------------------------------------------------------
// Toolbar
// -----------------------------------------------------------------------------
// Left  : [Project v][IDE]  |  Save Undo  |  T R S  |  Play Simulate  |  [Compile|v] status
// Right : [Speed / Volume]  Windows  Options  Lynxie
// The menus (v) group what is used less often : project, code editors, Ship
// Game, Reload Game, build options. Plugins : Options > Plugins (and their
// windows / tools in the Windows menu).









// =============================================================================
// Editor interface : the ONLY entry points used by main() and the GLFW callbacks.
// =============================================================================
// -----------------------------------------------------------------------------
// Editor camera persistence
// -----------------------------------------------------------------------------
// Stored in the project folder (working directory), NOT in assets/, so it is
// read/written with plain std::fstream instead of lynx::fs.
// Format : one line, "x y z".

static const char* const kEditorCameraFile = "editor_camera.txt";
static void LoadEditorCamera()
{
    std::ifstream file(kEditorCameraFile);

    if (!file)
        return;

    float x, y, z;

    if (!(file >> x >> y >> z))
        return;

    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return;

    camX = x;
    camY = y;
    camZ = ClampCameraZ(z);

    // Added later (optional) : mode, yaw, pitch.
    int mode = 0;
    float yaw = -90.f, pitch = 0.f;
    if (file >> mode >> yaw >> pitch && std::isfinite(yaw) && std::isfinite(pitch))
    {
        cameraMode = mode == 1 ? EditorCameraMode::Mode3D : EditorCameraMode::Mode2D;
        camYaw = yaw;
        camPitch = std::clamp(pitch, -89.f, 89.f);
        if (cameraMode == EditorCameraMode::Mode3D)
            camZ = z;
    }
}


// -----------------------------------------------------------------------------
// Voxel units
// -----------------------------------------------------------------------------
// The engine now has ONE unit : 1 = 1 voxel. A project made before stored
// positions in "world units" (one voxel = physical_voxel_size of them, 0.3 by
// default). Once, when it opens : the actor positions of its levels and the
// editor camera are divided by that size (the old files are kept as
// *.before-voxel-units), then settings.cfg says "units voxel".
// Sizes and speeds written in the game's code are NOT converted : the
// warning says so.
static void SaveSettings();

static void MigrateToVoxelUnits()
{
    if (appSettings.voxelUnits)
        return;

    const float size = appSettings.legacyVoxelSize;
    appSettings.voxelUnits = true;

    if (std::fabs(size - 1.f) < 1e-6f)
    {
        SaveSettings();
        return;
    }

    const float factor = 1.f / size;
    int converted_files = 0;
    std::error_code error;

    auto scale_number = [factor](const std::string& text) -> std::string
    {
        char* end = nullptr;
        const float value = std::strtof(text.c_str(), &end);
        if (end == text.c_str())
            return text;
        std::ostringstream out;
        out << std::setprecision(7) << value * factor;
        return out.str();
    };

    // Levels : transform="transform:x,y,z;r;s" -> x, y, z divided.
    const std::filesystem::path assets = std::filesystem::current_path() / "assets";
    for (std::filesystem::recursive_directory_iterator it(assets, error), end; !error && it != end; it.increment(error))
    {
        std::error_code e;
        if (!it->is_regular_file(e) || (it->path().extension() != ".xml" && it->path().extension() != ".level"))
            continue;

        std::ifstream in(it->path(), std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        if (text.find("<Level") == std::string::npos || text.find("transform:") == std::string::npos)
            continue;

        std::string out;
        size_t pos = 0;
        bool changed = false;
        for (;;)
        {
            const size_t start = text.find("transform:", pos);
            if (start == std::string::npos)
                break;
            const size_t values = start + 10;
            const size_t semicolon = text.find(';', values);
            const size_t quote = text.find('"', values);
            if (semicolon == std::string::npos || (quote != std::string::npos && quote < semicolon))
            {
                out += text.substr(pos, values - pos);
                pos = values;
                continue;
            }

            out += text.substr(pos, values - pos);
            const std::string location = text.substr(values, semicolon - values);
            std::string converted;
            size_t from = 0;
            for (int k = 0; k < 3; ++k)
            {
                const size_t comma = location.find(',', from);
                const std::string part = location.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
                converted += scale_number(part);
                if (comma == std::string::npos)
                    break;
                converted += ',';
                from = comma + 1;
            }
            out += converted;
            pos = semicolon;
            changed = true;
        }
        out += text.substr(pos);

        if (changed)
        {
            std::filesystem::copy_file(it->path(), it->path().string() + ".before-voxel-units",
                                       std::filesystem::copy_options::overwrite_existing, e);
            std::ofstream file(it->path(), std::ios::binary | std::ios::trunc);
            file << out;
            ++converted_files;
        }
    }

    // Editor camera.
    {
        std::ifstream in(kEditorCameraFile);
        float x, y, z;
        if (in >> x >> y >> z)
        {
            in.close();
            std::ofstream file(kEditorCameraFile, std::ios::trunc);
            file << std::setprecision(9) << x * factor << " " << y * factor << " " << z * factor << "\n";
        }
    }

    SaveSettings();

    std::ostringstream message;
    message << "This project used world units (1 voxel = " << size << " unit).\n"
            << "The engine now uses voxel units everywhere (1 unit = 1 voxel).\n\n"
            << "Converted : the actor positions of " << converted_files << " level file(s) (backup : "
            << "*.before-voxel-units) and the editor camera.\n"
            << "NOT converted : sizes, speeds and distances in the game's code and properties "
            << "(multiply them by " << factor << "), and the gameplay camera height.";
    ShowEditorWarning(message.str());
}


static void SaveEditorCamera()
{
    std::ofstream file(kEditorCameraFile, std::ios::trunc);

    if (!file)
    {
        std::cerr
            << "[CAMERA] Could not write "
            << kEditorCameraFile
            << "\n";
        return;
    }

    // 9 digits : enough to round-trip a float (world coordinates can be large).
    file << std::setprecision(9) << camX << " " << camY << " " << camZ << " "
         << static_cast<int>(cameraMode) << " " << camYaw << " " << camPitch << "\n";
}

// Rotation of the editor camera for the current mode.
static void ApplyEditorCameraRotation()
{
    if (editor_camera == HRL_INVALID_ID)
        return;

    if (cameraMode == EditorCameraMode::Mode3D)
        lynx::camera_state::SetRotation(editor_camera, camPitch, camYaw, 0.f);
    else
        lynx::camera_state::SetRotation(editor_camera, 0.f, -90.f, 0.f);
}

static void SetEditorCameraMode(EditorCameraMode mode)
{
    if (mode == cameraMode)
        return;

    cameraMode = mode;

    // Back to 2D : the height must be a valid 2D height again.
    if (mode == EditorCameraMode::Mode2D)
        camZ = ClampCameraZ(camZ);
    else
    {
        // Starts from the 2D view : same place, same direction.
        camYaw = -90.f;
        camPitch = 0.f;
    }

    ApplyEditorCameraRotation();
    if (editor_camera != HRL_INVALID_ID)
        lynx::camera_state::SetLocation(editor_camera, camX, camY, camZ);
    SaveEditorCamera();
}

// Direction the 3D camera looks at (pitch / yaw in degrees, -90 yaw = -Z).
static void EditorCameraAxes(float front[3], float right[3])
{
    const float yaw = camYaw * 3.14159265f / 180.f;
    const float pitch = camPitch * 3.14159265f / 180.f;

    front[0] = std::cos(yaw) * std::cos(pitch);
    front[1] = std::sin(pitch);
    front[2] = std::sin(yaw) * std::cos(pitch);

    // right = normalize(cross(front, up)) with up = +Y
    right[0] = -front[2];
    right[1] = 0.f;
    right[2] = front[0];
    const float len = std::sqrt(right[0] * right[0] + right[2] * right[2]);
    if (len > 1e-5f)
    {
        right[0] /= len;
        right[2] /= len;
    }
}


// Collision debug overlay (F4). The flag lives inside the game DLL, so it has to
// be pushed again every time the DLL is loaded (see editor::EndGameReload).
static bool show_collision_debug = false;


static void ApplyCollisionDebugToGame()
{
    // Every ColliderComponent of the level (editor, Play, Simulate).
    lynx::ColliderComponent::SetDebugDrawAll(show_collision_debug);
    // ... and every physics query (traces, overlaps).
    lynx::physics::SetDebugDrawAll(show_collision_debug);

    // Optional : a game without collision debug simply does not export it.
    if (gameHooks.set_collision_debug_enabled)
    {
        gameHooks.set_collision_debug_enabled(
            show_collision_debug ? 1 : 0
        );
    }
}


namespace editor
{
    constexpr bool kEnabled = true;

    // Returns the camera the viewport must start with (editor camera here).
    HRL_id CreateCamera(HRL_id, HRL_id)
    {
        editor_camera =
            HRL_CreateCamera(
                scene,
                HRL_PERSPECTIVE
            );

        lynx::camera_state::SetFov(
            editor_camera,
            20.f
        );

        // ------------------------------------------------------------
        // Camera
        // ------------------------------------------------------------

        HRL_VoxelToWorldCoordinates(
            scene,
            10,
            10,
            &camX,
            &camY
        );

        // Restore the last editor camera position, if one was saved.
        LoadEditorCamera();


        HRL_SetCameraNearPlane(
            editor_camera,
            0.1f
        );


        HRL_SetCameraFarPlane(
            editor_camera,
            10000.f   // voxel units : kCameraMaxZ and beyond
        );


        lynx::camera_state::SetLocation(
            editor_camera,
            camX,
            camY,
            camZ
        );


        // 2D : (0, -90, 0) ; 3D : the saved direction.
        ApplyEditorCameraRotation();



        return editor_camera;
    }


    // The interface (raven/EditorUi.inl).
    static void InitUi(Rid window);
    static void UpdateUi(float dt);
    static void ShutdownUi();

    void Init(
        Rid win,
        lynx::Engine* engine,
        HRL_id viewport,
        HRL_id gameplay_cam,
        lynx::Level* level,
        const std::string& save_data)
    {
        editor_engine = engine;
        editor_gameplay_cam = gameplay_cam;
        editor_viewport = viewport;
        editor_level = level;
        editor_save_data = save_data;

        content_browser_root =
            std::filesystem::current_path() / "assets";

        content_browser_current_path =
            content_browser_root;

        std::error_code assets_error;
        std::filesystem::create_directories(
            content_browser_root,
            assets_error
        );

        LoadEditorWindowVisibility();

        // Editor modules of the plugins (<Name>.Editor.dll).
        {
            lynx::editor::plugins::Host host;
            host.get_selected_actor = []() { return editing_actor; };
            host.select_actor = [](lynx::Actor* actor) { SetActorSelected(actor); };
            host.mark_level_dirty = []() { MarkEditorDirty(); };
            host.is_playing = []() { return isPlaying; };
            host.message = [](const std::string& text) { ShowEditorWarning(text); };
            host.open_asset = [](const std::filesystem::path& path) { content_browser_actions::OpenEntry(path); };
            host.project_root = currentProject.root;
            lynx::editor::plugins::SetHost(host);
            lynx::editor::plugins::LoadEditorModules();
        }

        // At every start : Commands is shown, on the Lynxie tab (whatever
        // was open / focused when the editor was closed).
        editorWindows.commands = true;
        lynx::editor::commands_window::ShowLynxie(5);

        // Console : "Fix with Lynxie" sends an error (stack, script, line) to Lynxie.
        lynx::editor::output_console::SetFixHandler([](const std::string& prompt, const std::string& display)
        {
            editorWindows.commands = true;
            SaveEditorWindowVisibility();
            lynx::editor::commands_window::AskLynxie(prompt, display);
        });


        // Apply persisted audio/camera preferences.
        lynx::SetMasterVolume(
            appSettings.masterVolume
        );

        cameraSpeed = appSettings.cameraSpeed;


        // ------------------------------------------------------------
        // Gizmo
        // ------------------------------------------------------------

        gizmo =
            HRL_CreateGizmo(
                viewport
            );

        // Same size on the screen at any zoom (voxel units : the level is big).
        HRL_SetGizmoUseScreenSize(gizmo, HRL_TRUE);
        HRL_SetGizmoScreenSize(gizmo, 110.f);


        HRL_SetGizmoVisible(
            gizmo,
            HRL_FALSE
        );


        HRL_SetGizmoMode(
            gizmo,
            HRL_GIZMO_MODE_TRANSLATE
        );


        HRL_SetGizmoSpace(
            gizmo,
            HRL_GIZMO_SPACE_WORLD
        );


        HRL_SetGizmoTranslateAxes(
            gizmo,
            HRL_GIZMO_AXIS_X |
            HRL_GIZMO_AXIS_Y |
            HRL_GIZMO_AXIS_Z
        );


        HRL_SetGizmoScreenSize(
            gizmo,
            100.0f
        );


        HRL_SetGizmoUseScreenSize(
            gizmo,
            HRL_TRUE
        );

        // Panels, toolbar, shortcuts (they read the state above).
        InitUi(win);
    }


    void Shutdown()
    {
        SaveEditorCamera();
        SaveEditorWindowVisibility();

        ShutdownUi();
    }


    // ------------------------------------------------------------------
    // Open another level (double-click on a .level in the Content Browser)
    // ------------------------------------------------------------------

    static std::string open_level_requested;

    // `level` : relative to assets/. Done by main() between two frames.
    void RequestOpenLevel(const std::string& level)
    {
        if (isPlaying)
        {
            ShowEditorWarning("Stop the game before opening another level.");
            return;
        }
        open_level_requested = level;
    }

    bool ConsumeOpenLevelRequest(std::string& level)
    {
        if (open_level_requested.empty())
            return false;
        level = open_level_requested;
        open_level_requested.clear();
        return true;
    }

    // Before the level is deleted : saves the current one (if it changed)
    // and forgets every actor pointer.
    void BeginOpenLevel()
    {
        if (editor_dirty)
            SaveEditor();

        editing_actor = nullptr;
        selected_actors.clear();
        editing_object = HRL_INVALID_ID;
        dragging_object = false;
        HRL_SetGizmoVisible(gizmo, HRL_FALSE);
        gizmoUndoActive = false;
        gizmoUndoActorId.clear();

        // Undo entries name actors of the old level.
        editorUndoHistory.clear();
        play_snapshot_valid = false;

        editor_level = nullptr;
    }

    // After the old level is deleted, before the new one : its voxel world
    // (created empty when the linked .hrlv does not exist yet).
    void LoadVoxelWorldOfLevel(const std::string& level)
    {
        current_level_file = level;

        const std::string voxels = lynx::Level::GetLinkedVoxelWorld(level);
        world_file_path_string = (std::filesystem::path("assets") / voxels).string();

        const auto data = lynx::fs::ReadBinary(voxels);
        if (!data.empty())
        {
            HRL_LoadVoxelWorldBuffer(scene, data.data(), data.size());
        }
        else
        {
            std::cout << "[WORLD] Creating new voxel world : " << world_file_path_string << "\n";
            // Exactly like a new project at startup : HRL_SetVoxelSize keeps
            // the world sparse (HRL_CreateVoxelWorld of this size fails, and
            // without a valid world nothing can be painted).
            HRL_SetVoxelSize(scene, 131072, 131072);
            HRL_SetVoxelChunkSize(scene, 16);
            HRL_SaveVoxelWorldAllFile(scene, world_file_path_string.c_str());
        }

        // Same settings as after the startup load : voxel types, render
        // mode, units, then an empty edit to (re)build the world.
        editor_save_data = voxels;
        lynx::voxels::ApplyToScene(scene);
        HRL_SetVoxelRenderMode(scene, appSettings.voxelRenderMode);
        HRL_SetVoxelPhysicalSize(scene, 1.f);
        HRL_BeginVoxelEdit(scene);
        HRL_EndVoxelEdit(scene);

        RememberLastLevel(level);
        std::cout << "[WORLD] Level " << level << " (voxels : " << voxels << ")\n";
    }

    void EndOpenLevel(lynx::Level* level)
    {
        editor_level = level;
        editor_dirty = false;
        ApplyCollisionDebugToGame();
    }


    // True once after the game was stopped and a Play snapshot exists.
    bool ConsumeRestoreRequest()
    {
        const bool requested =
            restore_after_play_requested;

        restore_after_play_requested = false;

        return requested;
    }


    // Called by main() right BEFORE the level is deleted and rebuilt from the
    // Play snapshot. Drops every actor pointer held by the editor.
    void BeginRestoreAfterPlay()
    {
        editing_actor = nullptr;
        selected_actors.clear();
        editing_object = HRL_INVALID_ID;
        dragging_object = false;

        HRL_SetGizmoVisible(
            gizmo,
            HRL_FALSE
        );

        gizmoUndoActive = false;
        gizmoUndoActorId.clear();

        // Undo entries refer to actors by ID and the restored level has the same
        // IDs, so the undo history is kept. editor_dirty is left untouched : the
        // restored state IS the state the user had before pressing Play.
        editor_level = nullptr;
    }


    // Called by main() AFTER the old level is deleted and BEFORE the new one is
    // created. Same order as at startup (voxel world first, then the level) :
    // reloading the voxel world while the actors' meshes already exist makes the
    // actors point to meshes that are gone (properties change, nothing moves).
    void RestoreVoxelsAfterPlay()
    {
        // In memory (SavePlaySnapshot) : no disk access.
        if (play_voxel_snapshot && !play_voxel_snapshot->empty())
        {
            HRL_LoadVoxelWorldBuffer(
                scene,
                play_voxel_snapshot->data(),
                play_voxel_snapshot->size()
            );
            play_voxel_snapshot.reset();
            return;
        }

        // The file (written by a worker : finished first).
        play_voxel_snapshot_write.Wait();

        auto voxel_data =
            lynx::fs::ReadBinary(
                kPlayBackupVoxelName
            );

        if (!voxel_data.empty())
        {
            HRL_LoadVoxelWorldBuffer(
                scene,
                voxel_data.data(),
                voxel_data.size()
            );
        }
    }


    // Called by main() once the level exists again.
    void EndRestoreAfterPlay(lynx::Level* level)
    {
        editor_level = level;

        play_snapshot_valid = false;

        // The new actors start with the default collision debug value.
        ApplyCollisionDebugToGame();

        std::cout << "[PLAY] State restored\n";
    }


    // True once per click on the toolbar "Reload Game" button.
    bool ConsumeReloadRequest()
    {
        const bool requested =
            reload_game_requested;

        reload_game_requested = false;

        return requested;
    }


    // Called by main() right BEFORE the actors are destroyed and the game DLL is
    // unloaded. After this call the editor holds no pointer to anything that
    // lives in the DLL.
    void BeginGameReload()
    {
        // 1. Leave play mode. EndGame() still runs while the DLL code is mapped.
        if (isPlaying)
        {
            SetPlaying(
                editor_engine,
                editor_viewport,
                editor_camera,
                false
            );

            isSimulating = false;
            editor_engine->SetSimulating(false);
        }

        // 2. The level is about to be rebuilt from its .level : write the pending
        //    edits first (this needs the actors to still be alive).
        if (editor_dirty)
            SaveEditor();

        // 3. Forget every actor pointer.
        editing_actor = nullptr;
        selected_actors.clear();
        editing_object = HRL_INVALID_ID;
        dragging_object = false;

        HRL_SetGizmoVisible(
            gizmo,
            HRL_FALSE
        );

        gizmoUndoActive = false;
        gizmoUndoActorId.clear();

        // 4. Undo entries refer to actors by ID : after the reload an ID could
        //    point to a brand new actor, so the history is dropped.
        editorUndoHistory.clear();

        // 5. The old level is going away.
        editor_level = nullptr;
    }


    // Called by main() once the DLL is loaded again and the new level exists.
    void EndGameReload(lynx::Level* level)
    {
        editor_level = level;
        editor_dirty = false;

        // The freshly loaded DLL starts with its own default value.
        ApplyCollisionDebugToGame();
    }


    // Shift lets the user interact with the editor without feeding the game.
    // Simulate : the game never gets the input (the editor camera moves).
    bool BlockGameInput()
    {
        return isSimulating || lynx::editor::input::Shift();
    }


    bool WantsMouse()
    {
        return !SceneAcceptsMouse(true);
    }


    void OnLeftMousePress()
    {
        if (isPlaying)
            return;

        double mouseX;
        double mouseY;

        lynx::editor::input::MousePosition(mouseX, mouseY);

        MouseToScene(mouseX, mouseY, mouseX, mouseY);

        const EditorIO io = EditorIO::Now();

        if ((!brushPaintEnabled || io.KeyCtrl) &&
            SceneAcceptsMouse(io.WantCaptureMouse))
        {
            HRL_id object =
                HRL_GL_GetHoveredObject(
                    scene,
                    (int)mouseX,
                    (int)mouseY,
                    nullptr
                );

            if (object != HRL_INVALID_ID)
            {
                editing_object = object;
                SetObjectSelected(object);
            }
            else
            {
                editing_object = HRL_INVALID_ID;
                SetObjectSelected(HRL_INVALID_ID);
            }

            // A click in the viewport : only this actor (or nothing).
            selected_actors.clear();
            if (editing_actor)
                selected_actors.push_back(editing_actor);
        }
    }


    void OnScroll(double yoffset)
    {
        if (!SceneAcceptsMouse(true))
            return;

        const EditorIO io = EditorIO::Now();

        // Ctrl + wheel = brush size.
        // Ctrl also freezes the camera Z.
        if (io.KeyCtrl)
        {
            if (brushShape != BrushShape::Single)
            {
                const int delta =
                    yoffset > 0.0
                        ? 1
                        : (yoffset < 0.0 ? -1 : 0);

                brushRadius =
                    std::clamp(
                        brushRadius + delta,
                        1,
                        256
                    );
            }

            return;
        }

        // 3D : the wheel moves the camera forward / backward.
        if (cameraMode == EditorCameraMode::Mode3D)
        {
            float front[3], right[3];
            EditorCameraAxes(front, right);
            const float step = static_cast<float>(yoffset) * cameraSpeed * 0.25f;
            camX += front[0] * step;
            camY += front[1] * step;
            camZ += front[2] * step;
            return;
        }

        // Normal wheel = camera Z.
        camZ =
            ClampCameraZ(
                camZ -
                static_cast<float>(yoffset) * std::max(2.f, camZ * 0.08f)
            );
    }


    // Everything editor related that runs BEFORE the engine update.
    void Tick(
        float dt)
    {
        HRL_id camera = editor_camera;

        // Editor modules of the plugins.
        lynx::editor::plugins::Tick(dt);

        // Simulate : the voxel physics runs around the editor camera too.
        if (isSimulating)
            lynx::voxel_physics::AddFocus(camX, camY);

        const EditorIO io = EditorIO::Now();

        double mouseX;
        double mouseY;

        lynx::editor::input::MousePosition(mouseX, mouseY);

        // C = pick the voxel under the mouse. HRL expects framebuffer
        // coordinates, while GLFW returns window coordinates. Do this
        // conversion independently from the editor viewport conversion.
        static bool c_was_down = false;
        const bool c_down =
            lynx::editor::keymap::Down(lynx::editor::keymap::Action::PickVoxel);

        if (c_down &&
            !c_was_down &&
            !isPlaying &&
            !io.WantTextInput &&
            sceneViewport.hovered)
        {
            // Use exactly the same screen-coordinate calculation as actor/voxel
            // placement. MouseToScene() is the authoritative conversion for the
            // editor, so do not apply a separate framebuffer scaling here.
            double screenMouseX = mouseX;
            double screenMouseY = mouseY;

            MouseToScene(
                screenMouseX,
                screenMouseY,
                screenMouseX,
                screenMouseY
            );

            int voxelX = 0;
            int voxelY = 0;

            if (HRL_GetVoxelAtScreenPosition(
                    scene,
                    static_cast<int>(screenMouseX),
                    static_cast<int>(screenMouseY),
                    &voxelX,
                    &voxelY))
            {
                brushVoxelType =
                    HRL_GetVoxelType(
                        scene,
                        voxelX,
                        voxelY
                    );
            }
        }

        c_was_down = c_down;

        // Window pixels -> scene pixels (brush, picking...).
        MouseToScene(mouseX, mouseY, mouseX, mouseY);


        // The brush preview is redrawn every frame at the end of the
        // brush block. Everything else in this frame sees the real voxels.
        ClearBrushPreview();


        // Shift = temporary eraser (voxel 0) while editing.
        // In play mode Shift only hands the mouse to the editor, so the
        // selected type is kept.
        brushActiveType =
            (io.KeyShift && !isPlaying)
                ? 0
                : brushVoxelType;




        // --------------------------------------------------------
        // F4 collision debug
        // --------------------------------------------------------

        const bool f4_down =
            lynx::editor::keymap::Down(lynx::editor::keymap::Action::CollisionDebug);


        if (f4_down && !f4_was_down)
        {
            show_collision_debug =
                !show_collision_debug;

            ApplyCollisionDebugToGame();
        }


        f4_was_down = f4_down;




        // --------------------------------------------------------
        // Ctrl + S
        // --------------------------------------------------------

        const bool ctrl_down =
            lynx::editor::input::Key(GLFW_KEY_LEFT_CONTROL) ||
            lynx::editor::input::Key(GLFW_KEY_RIGHT_CONTROL);


        // Ctrl+S by default (Options > Keyboard).
        const bool ctrl_s =
            lynx::editor::keymap::Down(lynx::editor::keymap::Action::Save);


        const bool javascript_editor_handles_shortcuts =
            !lynx::editor::documents::LevelTabActive() ||
            lynx::editor::script_editors::HasKeyboardFocus() ||
            lynx::editor::widget_editor::HasFocus() ||
            lynx::editor::graph_editors::HasFocus() ||
            lynx::editor::particle_editor::HasFocus() ||
            lynx::editor::vasm_editor::HasFocus() ||
            lynx::editor::plugins::HasFocus();


        if (ctrl_s &&
            !ctrl_s_was_down &&
            !javascript_editor_handles_shortcuts)
        {
            std::cout
                << "[CTRL+S] Saving world to: "
                << world_file_path_string
                << "\n";


            SaveEditor();
        }


        ctrl_s_was_down =
            ctrl_s;


        // --------------------------------------------------------
        // Camera movement
        // --------------------------------------------------------
        // Editor camera movement is independent from the voxel brush.
        // Simulate mode keeps it (the game runs under the editor camera).

        // 3D : free camera while the right mouse button is held over the
        // viewport (like Unreal).
        static bool flying = false;
        const bool rmb = lynx::editor::input::Mouse(GLFW_MOUSE_BUTTON_RIGHT);
        if (!rmb)
            flying = false;
        else if (!flying && sceneViewport.hovered && lynx::editor::input::MouseClicked(GLFW_MOUSE_BUTTON_RIGHT))
            flying = true;

        if (cameraMode == EditorCameraMode::Mode3D &&
            (!isPlaying || isSimulating))
        {
            if (flying && !io.WantTextInput)
            {
                camYaw += io.MouseDelta.x * 0.15f;
                camPitch = std::clamp(camPitch - io.MouseDelta.y * 0.15f, -89.f, 89.f);

                float front[3], right[3];
                EditorCameraAxes(front, right);

                float move[3] = { 0.f, 0.f, 0.f };
                auto add = [&](const float* axis, float k)
                {
                    move[0] += axis[0] * k; move[1] += axis[1] * k; move[2] += axis[2] * k;
                };
                const float up[3] = { 0.f, 1.f, 0.f };

                using lynx::editor::keymap::Action;
                if (lynx::editor::keymap::Down(Action::CameraForward)) add(front, 1.f);
                if (lynx::editor::keymap::Down(Action::CameraBack)) add(front, -1.f);
                if (lynx::editor::keymap::Down(Action::CameraRight)) add(right, 1.f);
                if (lynx::editor::keymap::Down(Action::CameraLeft)) add(right, -1.f);
                if (lynx::editor::keymap::Down(Action::CameraUp)) add(up, 1.f);
                if (lynx::editor::keymap::Down(Action::CameraDown)) add(up, -1.f);

                const float speed = cameraSpeed * 2.f * (io.KeyShift ? 3.f : 1.f) * dt;
                camX += move[0] * speed;
                camY += move[1] * speed;
                camZ += move[2] * speed;
            }

            ApplyEditorCameraRotation();
            lynx::camera_state::SetLocation(camera, camX, camY, camZ);
        }
        else if ((!isPlaying || isSimulating) &&
            !io.KeyCtrl &&
            !io.KeyAlt &&
            !io.WantTextInput)
        {
            // Speed depends on the camera height : slow when close, fast when far.
            const float moveSpeed = GetCameraSpeedForHeight();

            if (lynx::editor::keymap::Down(lynx::editor::keymap::Action::CameraForward))
                camY += moveSpeed * dt;

            if (!ctrl_down &&
                lynx::editor::keymap::Down(lynx::editor::keymap::Action::CameraBack))
                camY -= moveSpeed * dt;

            if (lynx::editor::keymap::Down(lynx::editor::keymap::Action::CameraRight))
                camX += moveSpeed * dt;

            if (lynx::editor::keymap::Down(lynx::editor::keymap::Action::CameraLeft))
                camX -= moveSpeed * dt;

            camZ = ClampCameraZ(camZ);

            lynx::camera_state::SetLocation(
                camera,
                camX,
                camY,
                camZ
            );
        }


        // Camera Shake window : Test shakes the editor camera.
        ApplyCameraShakePreview(camera, dt);


        // --------------------------------------------------------
        // Gizmo
        // --------------------------------------------------------

        if (!isPlaying &&
            editing_actor &&
            SceneAcceptsMouse(io.WantCaptureMouse))
        {
            float pos[3];
            float rot[3];
            float scl[3];

            ReadGizmo(pos, rot, scl);

            auto differs = [](const float* a, const float* b)
            {
                return a[0] != b[0] || a[1] != b[1] || a[2] != b[2];
            };

            const bool pos_changed   = differs(pos, gizmoLastPos);
            const bool rot_changed   = differs(rot, gizmoLastRot);
            const bool scale_changed = differs(scl, gizmoLastScale);

            if (pos_changed || rot_changed || scale_changed)
            {
                if (!gizmoUndoActive)
                {
                    gizmoUndoActive = true;
                    gizmoUndoActorId = editing_actor->object_id_;
                    gizmoUndoTransform = editing_actor->transform;
                    gizmoUndoOthers.clear();
                    PruneActorSelection();
                    for (lynx::Actor* other : selected_actors)
                        if (other != editing_actor)
                            gizmoUndoOthers.emplace_back(other->object_id_, other->transform);
                }

                dragging_object = true;

                if (pos_changed)
                {
                    // The other selected actors move by the same amount.
                    const lynx::vec3 delta(pos[0] - editing_actor->transform.location.x,
                                           pos[1] - editing_actor->transform.location.y,
                                           pos[2] - editing_actor->transform.location.z);
                    for (lynx::Actor* other : selected_actors)
                        if (other != editing_actor && IsActorAlive(other))
                            other->transform.location += delta;

                    editing_actor->transform.location = { pos[0], pos[1], pos[2] };
                }

                if (rot_changed)
                {
                    editing_actor->transform.rotation = { rot[0], rot[1], rot[2] };
                }

                if (scale_changed)
                {
                    editing_actor->transform.scale = { scl[0], scl[1], scl[2] };
                }

                for (int k = 0; k < 3; ++k)
                {
                    gizmoLastPos[k]   = pos[k];
                    gizmoLastRot[k]   = rot[k];
                    gizmoLastScale[k] = scl[k];
                }

                MarkEditorDirty();
            }
        }


        // Finish one gizmo movement as one undoable editor action.
        if (gizmoUndoActive &&
            !lynx::editor::input::Mouse(GLFW_MOUSE_BUTTON_LEFT))
        {
            const std::string actor_id =
                gizmoUndoActorId;

            const lynx::transform previous_transform =
                gizmoUndoTransform;
            const auto others = gizmoUndoOthers;

            PushEditorUndo([actor_id, previous_transform, others]()
            {
                lynx::Actor* actor =
                    FindEditorActor(actor_id);

                if (!actor)
                    return;

                actor->transform = previous_transform;

                std::vector<lynx::Actor*> moved = { actor };
                for (const auto& [id, tr] : others)
                    if (lynx::Actor* other = FindEditorActor(id))
                    {
                        other->transform = tr;
                        moved.push_back(other);
                    }
                SetActorSelection(moved, actor);
                MarkEditorDirty();
            });
            gizmoUndoOthers.clear();

            gizmoUndoActive = false;
            gizmoUndoActorId.clear();
        }



        if (brushPaintEnabled &&
            (!isPlaying || io.KeyShift))
        {
            // ----------------------------------------------------
            // Editor mouse interaction
            // ----------------------------------------------------

            if (SceneAcceptsMouse(io.WantCaptureMouse))
            {
                const bool leftMousePressed =
                    lynx::editor::input::Mouse(GLFW_MOUSE_BUTTON_LEFT);


                const bool dragShape = IsDragShape(brushShape);

                if (brushPaintEnabled &&
                    leftMousePressed &&
                    !brushPainting)
                {
                    brushPainting = true;
                    brushHasLastCenter = false;
                    brushPaintedVoxels.clear();
                    currentStrokeChanges.clear();
                    brushDragHasStart = false;

                    if (dragShape)
                    {
                        int startX;
                        int startY;

                        if (HRL_GetVoxelAtScreenPosition(
                                scene,
                                static_cast<int>(mouseX),
                                static_cast<int>(mouseY),
                                &startX,
                                &startY))
                        {
                            brushDragStartX = startX;
                            brushDragStartY = startY;
                            brushDragHasStart = true;
                        }
                    }
                }

                // Drag tools are committed only when the mouse is released:
                // press = start point, release = end point.
                if (dragShape &&
                    brushPainting &&
                    leftMousePressed &&
                    brushDragHasStart &&
                    !isPlaying)
                {
                    int endX;
                    int endY;

                    if (HRL_GetVoxelAtScreenPosition(
                            scene,
                            static_cast<int>(mouseX),
                            static_cast<int>(mouseY),
                            &endX,
                            &endY))
                    {
                        ApplyDragBrushPreview(
                            brushDragStartX,
                            brushDragStartY,
                            endX,
                            endY
                        );
                    }
                    else
                    {
                        ClearBrushPreview();
                    }
                }

                if (brushPaintEnabled &&
                    leftMousePressed &&
                    !dragShape)
                {
                    if (!brushHasLastCenter)
                        brushPaintedVoxels.clear();

                    int centerX;
                    int centerY;

                    if (HRL_GetVoxelAtScreenPosition(
                            scene,
                            static_cast<int>(mouseX),
                            static_cast<int>(mouseY),
                            &centerX,
                            &centerY))
                    {
                        paintBrushStroke(centerX, centerY);
                    }
                    else
                    {
                        // Cursor left the world : do not link across the gap.
                        brushHasLastCenter = false;
                    }
                }

                if ((!leftMousePressed ||
                     !brushPaintEnabled) &&
                    brushPainting)
                {
                    if (dragShape && brushDragHasStart)
                    {
                        int endX;
                        int endY;

                        if (HRL_GetVoxelAtScreenPosition(
                                scene,
                                static_cast<int>(mouseX),
                                static_cast<int>(mouseY),
                                &endX,
                                &endY))
                        {
                            ClearBrushPreview();
                            stampDragShape(
                                brushDragStartX,
                                brushDragStartY,
                                endX,
                                endY
                            );
                        }
                        else
                        {
                            ClearBrushPreview();
                        }
                    }

                    brushPainting = false;
                    brushHasLastCenter = false;
                    brushDragHasStart = false;
                    brushPaintedVoxels.clear();

                    if (!currentStrokeChanges.empty())
                    {
                        auto stroke =
                            std::move(currentStrokeChanges);

                        PushEditorUndo([stroke = std::move(stroke)]() mutable
                        {
                            for (auto it = stroke.rbegin();
                                 it != stroke.rend();
                                 ++it)
                            {
                                HRL_SetVoxelType(
                                    scene,
                                    it->x,
                                    it->y,
                                    it->previousType
                                );
                            }

                            MarkEditorDirty();
                        });
                    }

                    currentStrokeChanges.clear();
                }

                if ((!leftMousePressed || io.KeyCtrl) &&
                    !isPlaying)
                {
                    int centerX;
                    int centerY;

                    if (dragShape)
                    {
                        // A drag tool keeps showing its full preview between
                        // the press and release. Once released, fall back to
                        // the normal hover preview at the cursor.
                        if (brushDragHasStart)
                        {
                            // The release path above already committed it.
                            ClearBrushPreview();
                        }
                    }

                    if (HRL_GetVoxelAtScreenPosition(
                            scene,
                            static_cast<int>(mouseX),
                            static_cast<int>(mouseY),
                            &centerX,
                            &centerY))
                    {
                        if (dragShape)
                        {
                            // For drag tools, the hover state is a single
                            // point until a new drag begins.
                            ApplyBrushPreview(centerX, centerY);
                        }
                        else
                        {
                            ApplyBrushPreview(centerX, centerY);
                        }
                    }
                }

                // ------------------------------------------------
                // F1 save
                // ------------------------------------------------

                if (lynx::editor::input::Key(GLFW_KEY_F1))
                {
                    std::cout
                        << "[F1] Saving world to: "
                        << world_file_path_string
                        << "\n";


                    SaveEditor();
                }


                // ------------------------------------------------
                // F2 load
                // ------------------------------------------------

                if (lynx::editor::input::Key(GLFW_KEY_F2))
                {

                    auto world_save_data =
                        lynx::fs::ReadBinary(
                            editor_save_data
                        );


                    ClearBrushPreview();

                    HRL_LoadVoxelWorldBuffer(
                        scene,
                        world_save_data.data(),
                        world_save_data.size()
                    );
                }
            }
            else if (!SceneAcceptsMouse(io.WantCaptureMouse))
            {
                if (brushPainting)
                {
                    brushPainting = false;

                    brushPaintedVoxels.clear();


                    if (!currentStrokeChanges.empty())
                    {
                        auto stroke =
                            std::move(currentStrokeChanges);

                        PushEditorUndo([stroke = std::move(stroke)]() mutable
                        {
                            for (auto it = stroke.rbegin();
                                 it != stroke.rend();
                                 ++it)
                            {
                                HRL_SetVoxelType(
                                    scene,
                                    it->x,
                                    it->y,
                                    it->previousType
                                );
                            }

                            MarkEditorDirty();
                        });
                    }


                    currentStrokeChanges.clear();
                }
            }
        }



        // --------------------------------------------------------
        // Tab : toggle Painting / No Painting
        // --------------------------------------------------------
        static bool tab_was_down = false;

        const bool tab_down =
            lynx::editor::keymap::Down(lynx::editor::keymap::Action::TogglePaint);

        if (tab_down &&
            !tab_was_down &&
            !isPlaying &&
            !io.WantTextInput)
        {
            brushPaintEnabled = !brushPaintEnabled;

            // End any current stroke cleanly when leaving Painting mode.
            if (!brushPaintEnabled && brushPainting)
            {
                brushPainting = false;
                brushHasLastCenter = false;
                brushDragHasStart = false;
                brushPaintedVoxels.clear();

                if (!currentStrokeChanges.empty())
                {
                    auto stroke =
                        std::move(currentStrokeChanges);

                    PushEditorUndo([stroke = std::move(stroke)]() mutable
                    {
                        for (auto it = stroke.rbegin();
                             it != stroke.rend();
                             ++it)
                        {
                            HRL_SetVoxelType(
                                scene,
                                it->x,
                                it->y,
                                it->previousType
                            );
                        }

                        MarkEditorDirty();
                    });
                }

                currentStrokeChanges.clear();
            }
        }

        tab_was_down = tab_down;


        // --------------------------------------------------------
        // Space : cycle gizmo mode (translate -> rotate -> scale)
        // --------------------------------------------------------

        static bool space_was_down = false;

        const bool space_down =
            lynx::editor::keymap::Down(lynx::editor::keymap::Action::CycleGizmo);

        if (space_down &&
            !space_was_down &&
            !isPlaying &&
            !io.WantTextInput)
        {
            CycleGizmoMode();
        }

        space_was_down =
            space_down;


        // ------------------------------------------------------------
        // F3 : Toggle Editor / Game
        // ------------------------------------------------------------

        const bool f3_down =
            lynx::editor::keymap::Down(lynx::editor::keymap::Action::Play);

        if (f3_down && !f3_was_down)
            TogglePlayMode();

        f3_was_down =
            f3_down;


        // ------------------------------------------------------------
        // Alt + S : Simulate (or stop the game / the simulation)
        // ------------------------------------------------------------

        static bool alt_s_was_down = false;

        const bool alt_down =
            lynx::editor::input::Key(GLFW_KEY_LEFT_ALT) ||
            lynx::editor::input::Key(GLFW_KEY_RIGHT_ALT);

        (void)alt_down;
        const bool alt_s_down =
            lynx::editor::keymap::Down(lynx::editor::keymap::Action::Simulate) &&
            !io.WantTextInput;

        if (alt_s_down && !alt_s_was_down)
            TogglePlayMode(true);

        alt_s_was_down =
            alt_s_down;
    }




}





static double lastMouseX = 0.0;
static double lastMouseY = 0.0;
static bool haveLastMousePosition = false;










// -----------------------------------------------------------------------------
// Interface of main() (raven/EditorUi.inl, at the end of this file)
// -----------------------------------------------------------------------------
static bool editor_quit = false;                 // the editor window closes
static bool StartInterface();                    // Rinit, embedded files, themes
static void StopInterface();
static void SetProjectWindowIcon(Rid window, const std::vector<uint8_t>& png);
static void SetFpsText(double fps);
static void SetGameCursorHidden(bool hidden);   // the game hid the cursor (Play)


// -----------------------------------------------------------------------------
// Project selection
// -----------------------------------------------------------------------------
// Fills `project` from the command line (first argument = project folder) or
// from the project browser. Returns false when the user quits.
static bool ChooseProject(
    int argc,
    char** argv,
    lynx::host::GameProject& project)
{
    // "Open with Lynx" in the right-click menu of the folders (Explorer).
    lynx::editor::shell_integration::Register();

    std::string message;
    std::filesystem::path initial_path;

    if (argc > 1 && argv[1] && argv[1][0] != '\0')
    {
        initial_path = std::filesystem::path(argv[1]);

        std::string error;

        if (lynx::host::InspectProject(initial_path, project, error))
            return true;

        message = "Could not open the project given on the command line:\n" + error;
    }

    return lynx::editor::RunProjectBrowser(project, message, initial_path);
}


// Working directory = project root : every relative path of the engine
// ("assets/...") and of the editor (settings.cfg, imgui.ini...) points into it.
static bool EnterProjectDirectory(const lynx::host::GameProject& project)
{
    std::error_code error;
    std::filesystem::current_path(project.root, error);

    if (error)
    {
        std::cerr << "[PROJECT] Could not enter " << project.root.string()
                  << " : " << error.message() << "\n";
        return false;
    }

    std::cout << "[PROJECT] " << project.name << " (" << project.root.string() << ")\n"
              << "[PROJECT] Game DLL : " << project.module_path.string() << "\n";

    return true;
}


// Compiles the game before loading it when needed (DLL missing, older than the
// engine, or older than its sources). A DLL built against another version of
// the engine is NEVER loaded : it would crash the editor.
// Returns true when currentProject.module_path can be loaded. false : go back
// to the project browser with `message`, or quit when `message` is empty.
static bool PrepareGameModule(lynx::host::GameProject& project, std::string& message)
{
    message.clear();

    for (;;)
    {
        bool blocking = false;
        const std::string reason = lynx::host::GetBuildReason(project, blocking);

        if (reason.empty())
            return true;

        if (!lynx::host::CanBuildProject(project.root))
        {
            // Nothing to build with : the user builds it himself.
            if (!blocking)
                return true;

            message = reason + "\n\nNo Build.bat / CMakeLists.txt in the project : build the game, then open it again.";
            return false;
        }

        lynx::editor::game_build::Init(project.root, project.module_path);

        // The build window takes over : no splash above it meanwhile.
        lynx::editor::splash::Hide();

        const lynx::editor::StartupBuildResult build_result =
            lynx::editor::RunStartupBuild(project, reason, !blocking);

        if (build_result == lynx::editor::StartupBuildResult::OpenAnyway ||
            build_result == lynx::editor::StartupBuildResult::Built)
        {
            lynx::editor::splash::Show();
            lynx::editor::splash::SetStep("Game compiled", 0.18f);
        }

        switch (build_result)
        {
        case lynx::editor::StartupBuildResult::OpenAnyway:
            return true;

        case lynx::editor::StartupBuildResult::OtherProject:
            message = "Choose a project.";
            return false;

        case lynx::editor::StartupBuildResult::Quit:
            return false;

        case lynx::editor::StartupBuildResult::Built:
            break;
        }

        // The DLL may not have existed before : look for it again (same file
        // name as before when there are several).
        const std::filesystem::path previous = project.module_path;
        lynx::host::GameProject rebuilt;
        std::string error;

        if (!lynx::host::InspectProject(project.root, rebuilt, error) || rebuilt.modules.empty())
        {
            message = "The build succeeded, but no game DLL was found in build/ "
                      "(a DLL exporting FactoryRegisterClasses, see LYNX_LINK_MODULE).";
            return false;
        }

        for (const auto& module : rebuilt.modules)
        {
            if (!previous.empty() && module.filename() == previous.filename())
            {
                rebuilt.module_path = module;
                break;
            }
        }

        project = rebuilt;

        std::cout << "[PROJECT] Game DLL : " << project.module_path.string() << "\n";

        // Still "out of date" after a successful build : the build tool found
        // nothing to do (ex : only a file outside the build was touched). The
        // DLL matches its sources : load it rather than building forever.
        bool still_blocking = false;
        const std::string still = lynx::host::GetBuildReason(project, still_blocking);

        if (!still.empty())
            std::cout << "[BUILD] Nothing was rebuilt (" << still << ") : loading the DLL.\n";

        return true;
    }
}


// input.json (key bindings) is read with a plain file path, not through
// lynx::fs. Its place is the project root ; build/ (the old working directory)
// and the editor folder are fallbacks.
static void LoadInputConfig()
{
    const std::filesystem::path candidates[] = {
        std::filesystem::path("input.json"),
        currentProject.module_path.parent_path() / "input.json",
        currentProject.build_dir / "input.json",
        lynx::host::GetEditorDirectory() / "input.json",
    };

    for (const auto& candidate : candidates)
    {
        std::error_code error;

        if (candidate.empty() || !std::filesystem::is_regular_file(candidate, error))
            continue;

        std::cout << "[INPUT] Bindings : " << candidate.string() << "\n";

        lynx::LoadConfigFile(
            candidate.string().c_str()
        );
        return;
    }

    std::cerr << "[INPUT] input.json not found in the project root : no key binding "
                 "(create them in the \"Input Settings\" window).\n";
}


#ifdef _WIN32
// Last resort : a crash that nothing caught. Instead of the editor vanishing
// without a word, say where it happened (game DLL -> recompile the game).
static LONG WINAPI EditorCrashFilter(EXCEPTION_POINTERS* info)
{
    const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    void* address = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;

    wchar_t module_file[MAX_PATH] = L"?";
    uintptr_t offset = 0;
    HMODULE module = nullptr;

    if (address &&
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCWSTR>(address), &module) &&
        module)
    {
        GetModuleFileNameW(module, module_file, MAX_PATH);
        offset = reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module);
    }

    const std::wstring file = module_file;
    const std::wstring name = std::filesystem::path(file).filename().wstring();

    // The game DLL is loaded from its shadow copy (%TEMP%\\LynxEditor\\<pid>).
    const bool in_game =
        file.find(L"LynxEditor") != std::wstring::npos ||
        (!currentProject.module_path.empty() &&
         name.find(currentProject.module_path.stem().wstring()) == 0);

    wchar_t text[1024];
    swprintf(text, 1024,
             L"The editor crashed (exception 0x%08lX) in %ls at +0x%llX.\n\n%ls",
             static_cast<unsigned long>(code), name.c_str(),
             static_cast<unsigned long long>(offset),
             in_game
                 ? L"The crash comes from the game code. If the engine was changed, compile the game "
                   L"again (the editor compiles it when opening the project if it is out of date)."
                 : L"Unsaved changes are lost. Check the console for the last messages.");

    std::wcerr << L"[CRASH] " << text << L"\n";
    std::wcerr.flush();

    MessageBoxW(nullptr, text, L"Lynx Editor", MB_OK | MB_ICONERROR | MB_TOPMOST);

    return EXCEPTION_EXECUTE_HANDLER;
}
#endif


#ifdef _WIN32
// find_package(Lynx) in the builds started by the editor (Build.bat, cmake, the
// IDE opened from the editor) : the environment variable Lynx_DIR points to the
// engine of THIS editor, so the game is compiled against the engine that loads it,
// even on a computer without the engine sources (engine installed by the launcher).
//   engine build folder : <build>/LynxConfig.cmake (headers of the source tree)
//   installed version   : <version>/sdk/cmake/LynxConfig.cmake
// A Lynx_DIR set by the user is kept.
static void ExportEngineLocationForCMake()
{
    if (GetEnvironmentVariableW(L"Lynx_DIR", nullptr, 0) > 0)
        return;

    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;

    const std::filesystem::path directory = std::filesystem::path(buffer).parent_path();
    for (const std::filesystem::path& candidate : { directory, directory / "sdk" / "cmake" })
    {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate / "LynxConfig.cmake", error))
        {
            SetEnvironmentVariableW(L"Lynx_DIR", candidate.wstring().c_str());
            return;
        }
    }
}
#endif


int main(int argc, char** argv)
{
    // No Windows console (-mwindows) : stdout / stderr go to the Console window.
    lynx::editor::output_console::Start();

#ifdef _WIN32
    SetUnhandledExceptionFilter(EditorCrashFilter);
    ExportEngineLocationForCMake();
#endif

    // Raven : every window of the editor (project browser, splash, editor).
    if (!StartInterface())
        return 1;
    // ------------------------------------------------------------
    // Project
    // ------------------------------------------------------------

    if (!ChooseProject(argc, argv, currentProject))
        return 0;

    // From here to the first editor frame : splash screen (see SplashScreen.h).
    // The editor window stays hidden until everything is loaded.
    lynx::editor::splash::Open(currentProject);

    // Back to the project browser (the splash hides meanwhile). false : the
    // user closed it, the editor quits.
    auto browse_again = [&](const std::string& message) -> bool
    {
        lynx::editor::splash::Hide();

        if (!lynx::editor::RunProjectBrowser(currentProject, message, currentProject.root))
        {
            lynx::editor::splash::Close();
            return false;
        }

        lynx::editor::splash::Open(currentProject);
        return true;
    };

    while (!EnterProjectDirectory(currentProject))
    {
        if (!browse_again("Could not open this project folder."))
            return 0;
    }

    lynx::editor::splash::SetStep("Starting the engine", 0.05f);

    // The engine reads "assets/" from the working directory : create it only
    // once we are in the project.
    // Release mode (packed assets) : lynx::Engine::SetReleaseMode(true) before.
    lynx::Engine* engine =
        lynx::Engine::Create();

    // The game module is owned through a pointer so the editor can unload it and
    // load it again (toolbar "Reload Game"). Destroying it (reset / destructor)
    // invalidates everything that comes from the DLL : see SysModule::Unload().
    std::unique_ptr<lynx::SysModule> gameModule;

    // Loads the project's game DLL. The absolute path makes Windows also look
    // for its dependencies in build/ ; lynx.dll / hrl.dll already loaded by the
    // editor are shared with it.
    //
    // The editor loads a COPY of the DLL (temp folder) : build/<game>.dll is
    // never locked, the game can be rebuilt while the editor runs.
    std::filesystem::path loadedModuleCopy;

    auto load_module_file = [&](const std::filesystem::path& file) -> bool
    {
        gameModule =
            std::make_unique<lynx::SysModule>(
                file.string().c_str()
            );

        if (!gameModule->IsLoaded())
        {
            gameModule.reset();
            return false;
        }

        gameModule->RegisterFactory();
        gameHooks.Load(*gameModule);

        return true;
    };

    auto load_game_module = [&]() -> bool
    {
#ifdef _WIN32
        // The copy lives in another folder : DLLs the game needs that are next
        // to it in build/ must still be found.
        SetDllDirectoryW(currentProject.module_path.parent_path().wstring().c_str());
#endif

        std::string copy_error;
        const std::filesystem::path copy =
            lynx::editor::game_build::MakeShadowCopy(currentProject.module_path, copy_error);

        if (copy.empty())
        {
            // No copy : load the file itself (it stays locked until exit).
            std::cerr << "[RELOAD] " << copy_error << " -> loading the DLL directly\n";

            if (!load_module_file(currentProject.module_path))
                return false;

            loadedModuleCopy.clear();
        }
        else
        {
            if (!load_module_file(copy))
                return false;

            loadedModuleCopy = copy;
        }

        lynx::editor::game_build::MarkModuleLoaded();
        return true;
    };

    // Compile the game first if needed, then load it. Any failure goes back
    // to the project browser : the editor never runs with a broken game DLL.
    {
        std::string message;

        for (;;)
        {
            lynx::editor::splash::SetStep("Checking the game build", 0.10f);

            if (PrepareGameModule(currentProject, message))
            {
                lynx::editor::splash::SetStep(
                    "Loading the game module (" +
                        currentProject.module_path.filename().string() + ")",
                    0.20f);

                if (load_game_module())
                    break;

                message =
                    "Could not load the game DLL:\n" +
                    currentProject.module_path.string() +
                    "\n\nIt must be built against this version of the Lynx engine, and the DLLs it "
                    "needs (other than lynx.dll) must be next to it in build/.";
            }

            if (message.empty())
            {
                lynx::editor::splash::Close();
                return 0;   // Quit
            }

            std::cerr << "[PROJECT] " << message << "\n";

            if (!browse_again(message))
                return 0;

            if (!EnterProjectDirectory(currentProject))
            {
                lynx::editor::splash::Close();
                return 1;
            }
        }
    }

    lynx::host::AddRecentProject(currentProject.root);

    // Plugins : <editor>/plugins (engine) and <project>/plugins, enabled in
    // <project>/plugins.json. Runtime modules now (classes before the level).
    lynx::editor::splash::SetStep("Loading the plugins", 0.24f);
    lynx::plugins::Discover(
        lynx::host::GetEditorDirectory() / "plugins",
        currentProject.root / "plugins",
        currentProject.root);
    lynx::plugins::LoadRuntimeModules();

    lynx::editor::game_build::Init(currentProject.root, currentProject.module_path);

    // Game DLL not rebuilt after an engine change : warn (crash otherwise).
    ShowEditorWarning(
        currentProject.script_only ? std::string() : lynx::host::CheckModuleUpToDate(currentProject.module_path)
    );

    // Project files (working directory = project root).
    lynx::editor::splash::SetStep("Reading the project settings", 0.28f);

    LoadSettings();
    MigrateToVoxelUnits();

    LoadInputConfig();


    lynx::editor::splash::SetStep("Creating the editor window", 0.32f);

    // Gamepads only (GLFW opens no window).
    glfwInit();

    const std::string window_title =
        "Lynx - " + currentProject.name;

    // Hidden until the project is loaded : the splash is shown meanwhile, the
    // window appears (maximized) once its content is ready.
    const Rid win =
        lynx::editor::ui::LoadWindow("editor.xml");

    if (win == R_INVALID_ID)
    {
        std::cerr << "[WINDOW] Could not create the editor window\n";
        lynx::editor::splash::Close();
        return 1;
    }

    Rwindow_set_title(win, window_title.c_str());

    // icon.png of the project (assets) ; otherwise the icon of the executable.
    SetProjectWindowIcon(win, lynx::fs::ReadBinary("icon.png"));

    Rset_vsync(appSettings.vsync ? R_TRUE : R_FALSE);


    lynx::editor::splash::SetStep("Starting the renderer", 0.38f);

    // HRL keeps OpenGL 3.3 : a context of its own (hidden window), the scene
    // is rendered off-screen and shown in the Viewport panel (GlBridge).
    if (!lynx::editor::gl::Init())
    {
        std::cerr << "[WINDOW] OpenGL 3.3 is not available\n";
        lynx::editor::splash::Close();
#ifdef _WIN32
        MessageBoxW(nullptr, L"OpenGL 3.3 is not available on this computer : the renderer cannot start.",
                    L"Lynx Editor", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }

    HRL_Init(HRL_OPENGL_33);

    int winX = 1280, winY = 720;

    HRL_InitContext(
        winX,
        winY,
        (void*)lynx::editor::gl::GetProcAddress
    );

    // Size known by the widgets too (the Viewport panel sets its own).
    engine->SetRenderSize(winX, winY);

    HRL_RegisterErrorCallback(
        ErrorCallback
    );


    // Needs HRL (texture loading) : after HRL_InitContext, before editor::Init.

    HRL_SetDebugLineThickness(
        3.f
    );


    // The scene belongs to the engine (Engine::GetScene()) : it needs the
    // HRL context, created just above.
    scene =
        engine->CreateScene();


    // The default player (player 0) got its viewport and its camera with the
    // scene : its camera is the gameplay camera (Play, game hooks).
    lynx::PlayerController* defaultPlayer =
        engine->GetDefaultPlayer();

    HRL_id gameplay_cam =
        defaultPlayer->GetDefaultCamera();


    // ------------------------------------------------------------
    // Load world
    // ------------------------------------------------------------
    // The level (.level, relative to assets/) is linked to its voxel world
    // (.hrlv). Startup : the last level opened in the editor, otherwise the
    // startup level of the game (assets/save_file.txt). Old projects
    // (save_file.txt = voxel world, actors in world.xml) are converted once.

    lynx::editor::splash::SetStep("Loading the voxel world", 0.48f);

    {
        std::string legacy_voxels;
        std::string level_file = lynx::Level::ResolveStartupLevel(&legacy_voxels);

        if (!legacy_voxels.empty())
            level_file = MigrateLegacyLevel(level_file, legacy_voxels);

        const std::string last = ReadLastLevel();
        if (!last.empty() && std::filesystem::exists(std::filesystem::path("assets") / last))
            level_file = last;

        current_level_file = level_file;
    }

    std::string save_data =
        lynx::Level::GetLinkedVoxelWorld(current_level_file);

    std::cout
        << "[WORLD] Level : " << current_level_file
        << ", voxel world : " << save_data << "\n";


    std::filesystem::path world_path =
        std::filesystem::path(save_data);


    std::filesystem::path world_file_path =
        std::filesystem::path("assets") /
        world_path;


    std::string world_path_string =
        world_path.string();


    world_file_path_string =
        world_file_path.string();


    std::cout
        << "[WORLD] Relative path : "
        << world_path_string
        << "\n";

    std::cout
        << "[WORLD] File path : "
        << world_file_path_string
        << "\n";


    if (lynx::fs::Exists(world_path_string))
    {
        std::cout
            << "[WORLD] Loading : "
            << world_path_string
            << "\n";


        auto world_file =
            lynx::fs::ReadBinary(
                world_path_string
            );


        std::cout
            << "[WORLD] Loaded "
            << world_file.size()
            << " bytes\n";


        if (world_file.empty())
        {
            std::cerr
                << "[WORLD] ERROR: world file is empty.\n";
        }
        else
        {
            HRL_LoadVoxelWorldBuffer(
                scene,
                world_file.data(),
                world_file.size()
            );
        }
    }
    else
    {
        std::cout
            << "[WORLD] Creating new world : "
            << world_file_path_string
            << "\n";


        HRL_SetVoxelSize(
            scene,
            131072,
            131072
        );

        HRL_SetVoxelChunkSize(
            scene,
            16
        );


        HRL_SaveVoxelWorldAllFile(
            scene,
            world_file_path_string.c_str()
        );
    }


    // Voxel types (assets/voxels.json) : before the level, the actors may
    // need them when they are created.
    lynx::editor::splash::SetStep("Loading the voxel types", 0.64f);

    if (!lynx::voxels::LoadFile("voxels.json"))
        ShowEditorWarning("Voxel types not loaded:\n" + lynx::voxels::GetLoadError());

    lynx::voxels::ApplyToScene(scene);

    // The game registers its voxel events...
    if (gameHooks.setup_scene)
        gameHooks.setup_scene(scene);
    lynx::plugins::SetupScene(scene);


    lynx::editor::splash::SetStep("Loading the level", 0.72f);

    auto* level = engine->CreateLevel(
        current_level_file.c_str()
    );

    auto font_data =
        lynx::fs::ReadBinary(
            "normal-font.ttf"
        );


    if (!font_data.empty())
    {
        HRL_id hrlfont =
            HRL_CreateFont(
                reinterpret_cast<const char*>(
                    font_data.data()
                ),
                font_data.size()
            );


        HRL_SetDebugMeshInfoTextSize(
            scene,
            32.f
        );

        HRL_SetDebugMeshInfoFont(
            scene,
            hrlfont
        );

        HRL_SetDebugMeshInfoTextColor(
            scene,
            1,
            0.2,
            0.2,
            1.0
        );
    }



    // One unit everywhere : 1 world unit = 1 voxel.
    lynx::editor::splash::SetStep("Preparing the viewport", 0.80f);

    HRL_SetVoxelPhysicalSize(scene, 1.f);

    // New project (project browser > New project) : first level of its template,
    // painted once (.lynx/starter_terrain.txt, deleted afterwards).
    lynx::editor::project_templates::ApplyStarterTerrain(
        scene,
        world_file_path_string
    );


    // ------------------------------------------------------------
    // Viewport (starts on the editor camera)
    // ------------------------------------------------------------

    HRL_id start_camera =
        editor::CreateCamera(
            scene,
            gameplay_cam
        );


    // No viewport of its own : the editor shows the one of the default player
    // (each PlayerController owns its viewport).
    HRL_id viewport =
        defaultPlayer->GetViewportBackend();

    defaultPlayer->SetViewCamera(start_camera);


    lynx::editor::splash::SetStep("Starting the editor", 0.86f);

    editor::Init(
        win,
        engine,
        viewport,
        gameplay_cam,
        level,
        save_data
    );

    // Input Settings window : edits the project's input.json (project root).
    lynx::editor::input_settings::Init(
        nullptr,
        currentProject.root / "input.json"
    );

    // Code editors : the JavaScript language service reads the project.
    lynx::editor::script_editors::SetProjectRoot(currentProject.root);

    // Commands (Python scripts, AI through MCP) : registry, local server,
    // script runner. The python/ folder (lynx_editor module, MCP server) is
    // copied next to the editor by CMake.
    lynx::editor::splash::SetStep("Starting the command server", 0.93f);

    RegisterEditorCommands();

    lynx::editor::script_runner::Init(
        currentProject.root,
        lynx::host::GetEditorDirectory() / "python"
    );

    lynx::editor::command_server::Start(currentProject.root);


    // Apply the persisted voxel surface mode after the voxel world and its
    // type definitions are ready.
    HRL_SetVoxelRenderMode(
        scene,
        appSettings.voxelRenderMode
    );


    // Post process of every player (HRL default shader). Its settings come
    // from assets/postprocess.json (editor : Windows > Post Process).
    lynx::postprocess::Install();
    lynx::lighting2d::Install();   // 2D lighting pass (before the post process)

    // Camera shake settings (editor : Windows > Camera Shake).
    lynx::LoadCameraShakeSettings();


    // ------------------------------------------------------------
    // FPS
    // ------------------------------------------------------------

    double fpsTimer =
        glfwGetTime();

    int fpsFrames = 0;

    double lastFrameTime =
        glfwGetTime();


    HRL_BeginVoxelEdit(scene);


    HRL_EndVoxelEdit(scene);


    // The game places its gameplay camera (ex : on the player).
    if (gameHooks.on_level_loaded)
        gameHooks.on_level_loaded(gameplay_cam);


    // ------------------------------------------------------------
    // Loaded : the editor window appears behind the splash, which fades out
    // after the first editor frames (splash::Tick in the main loop).
    // ------------------------------------------------------------

    lynx::editor::splash::Complete();

    Rwindow_maximize(win);
    Rwindow_show(win);

    // "Buy me a coffee" : a few seconds after the project opens, then from
    // time to time (never while the game is playing).
    lynx::editor::donation::Start(win, []() { return isPlaying; });

    // The loading time is not a frame.
    lastFrameTime =
        glfwGetTime();


    // ------------------------------------------------------------
    // Game module reload (editor toolbar "Reload Game")
    // ------------------------------------------------------------
    // Returns false when the game cannot go on (the window is then closed).

    auto reload_game_module = [&]() -> bool
    {

        std::cout << "[RELOAD] Reloading "
                  << currentProject.module_path.filename().string()
                  << "\n";

        // 1. Editor side : stops the game, saves pending edits, drops every actor
        //    pointer / selection / undo entry.
        editor::BeginGameReload();

        // 2. Unload the DLL. SysModule::Unload() first calls
        //    engine->DeleteCurrentLevel() (every actor dies while their code is
        //    still mapped), then unregisters the factory, then FreeLibrary.
        //    `level` is dangling from here until CreateLevel() below.
        //    The hooks point into the DLL : forget them first.
        gameHooks.Clear();

        gameModule.reset();
        lynx::profiler::Reset();   // zone names of the unloaded DLL

        level = nullptr;

        const std::filesystem::path previousCopy = loadedModuleCopy;

        // 3. Load the DLL again and register its actor classes.
        if (!load_game_module())
        {
            std::cerr << "[RELOAD] ERROR: could not load "
                      << currentProject.module_path.string()
                      << "\n";

            // Back to the previous build (its copy is still on disk).
            if (previousCopy.empty() || !load_module_file(previousCopy))
            {
                // No actor class : rebuilding the level now would lose every
                // actor at the next save. Stop here (saved in step 1).
                return false;
            }

            loadedModuleCopy = previousCopy;

            // Do not retry this build in a loop.
            lynx::editor::game_build::MarkModuleLoaded();

            ShowEditorWarning(
                "The new game DLL could not be loaded :\n" +
                currentProject.module_path.string() +
                "\n\nThe previous build is still used."
            );
        }

        module_changed_notice = false;

        // Voxel types : assets/voxels.json may have been edited too.
        if (lynx::voxels::LoadFile("voxels.json"))
            lynx::voxels::ApplyToScene(scene);
        else
            ShowEditorWarning("Voxel types not reloaded:\n" + lynx::voxels::GetLoadError());

        // Voxel events of the new build.
        if (gameHooks.setup_scene)
            gameHooks.setup_scene(scene);
        lynx::plugins::SetupScene(scene);

        // 4. Rebuild the level (saved in step 1 if it was dirty).
        level =
            engine->CreateLevel(
                current_level_file.c_str()
            );

        if (!level)
        {
            std::cerr << "[RELOAD] ERROR: could not recreate the level.\n";
            return false;
        }

        if (gameHooks.on_level_loaded)
            gameHooks.on_level_loaded(gameplay_cam);

        editor::EndGameReload(level);

        ShowEditorWarning(
            currentProject.script_only ? std::string() : lynx::host::CheckModuleUpToDate(currentProject.module_path)
        );

        std::cout << "[RELOAD] Done\n";

        return true;
    };


    // ------------------------------------------------------------
    // Main loop
    // ------------------------------------------------------------

    LYNX_PROFILE_THREAD("Main");

    while (!editor_quit && Rwindow_is_valid(win))
    {
        {
            LYNX_PROFILE_SCOPE("Events");
            glfwPollEvents();   // gamepads
            Rpoll_events();
        }


        // --------------------------------------------------------
        // Game DLL reload : between two frames, no ImGui frame is open
        // --------------------------------------------------------

        // --------------------------------------------------------
        // Hot reload : "Compile" finished, or the DLL was rebuilt outside
        // --------------------------------------------------------

        {
            bool build_success = false;

            const bool build_finished =
                lynx::editor::game_build::PollFinished(build_success);

            // editor.compile commands waiting for the end of the build.
            if (build_finished)
                NotifyCommandBuildFinished(build_success);

            if (build_finished && build_success)
            {
                if (appSettings.reloadAfterBuild)
                {
                    RequestHotReload();
                }
                else
                {
                    // Built on purpose without reload : no automatic reload
                    // from the file watcher either, just the notice.
                    lynx::editor::game_build::MarkModuleLoaded();
                    module_changed_notice = true;
                }
            }

            if (lynx::editor::game_build::ModuleChangedOnDisk())
            {
                if (appSettings.autoReloadOnChange)
                {
                    if (!hot_reload_pending && !reload_game_requested)
                    {
                        std::cout << "[RELOAD] " << currentProject.module_path.filename().string()
                                  << " was rebuilt\n";
                        RequestHotReload();
                    }
                }
                else
                {
                    module_changed_notice = true;
                }
            }

            if (hot_reload_pending && !isPlaying)
            {
                hot_reload_pending = false;
                reload_game_requested = true;
            }
        }

        if (editor::ConsumeReloadRequest())
        {
            if (!reload_game_module())
            {
                editor_quit = true;

                continue;
            }

            // Do not count the reload time as a frame.
            lastFrameTime =
                glfwGetTime();
        }


        // --------------------------------------------------------
        // Open another level (Content Browser) : same idea as the restore
        // below, with the voxel world linked to the new level.
        // --------------------------------------------------------

        if (std::string next_level; editor::ConsumeOpenLevelRequest(next_level))
        {
            editor::BeginOpenLevel();

            engine->DeleteCurrentLevel();

            editor::LoadVoxelWorldOfLevel(next_level);

            level =
                engine->CreateLevel(
                    next_level.c_str()
                );

            if (!level)
            {
                std::cerr << "[WORLD] ERROR: could not open " << next_level << "\n";

                editor_quit = true;

                continue;
            }

            if (gameHooks.on_level_loaded)
                gameHooks.on_level_loaded(gameplay_cam);

            editor::EndOpenLevel(level);

            lastFrameTime =
                glfwGetTime();
        }


        // --------------------------------------------------------
        // Restore the pre-Play state (after Stop) : same idea as the DLL
        // reload, but the module stays loaded.
        // --------------------------------------------------------

        if (editor::ConsumeRestoreRequest())
        {
            editor::BeginRestoreAfterPlay();

            engine->DeleteCurrentLevel();

            // Voxel world first, level second (same order as the startup).
            editor::RestoreVoxelsAfterPlay();

            level =
                engine->CreateLevel(
                    "play_backup.xml"
                );

            if (!level)
            {
                std::cerr << "[PLAY] ERROR: could not restore the level.\n";

                editor_quit = true;

                continue;
            }

            if (gameHooks.on_level_loaded)
                gameHooks.on_level_loaded(gameplay_cam);

            editor::EndRestoreAfterPlay(level);

            lastFrameTime =
                glfwGetTime();
        }


        // --------------------------------------------------------
        // Commands from Python scripts / AI tools : between two frames,
        // after the reloads / restores above (the level is valid).
        // --------------------------------------------------------

        lynx::editor::commands_window::Update();
        git_window::Update();
        lynx::editor::command_server::Poll();


        // --------------------------------------------------------
        // Delta time
        // --------------------------------------------------------

        double currentFrameTime =
            glfwGetTime();


        float dt =
            static_cast<float>(
                currentFrameTime -
                lastFrameTime
            );


        lastFrameTime =
            currentFrameTime;


        dt =
            std::min(
                dt,
                0.1f
            );




        // --------------------------------------------------------
        // FPS counter
        // --------------------------------------------------------

        fpsFrames++;


        double now =
            glfwGetTime();


        if (now - fpsTimer >= 0.25)
        {
            double fps =
                static_cast<double>(fpsFrames) /
                (now - fpsTimer);


            SetFpsText(fps);


            fpsFrames = 0;

            fpsTimer = now;
        }


        // --------------------------------------------------------
        // Editor (camera, gizmo, brush, shortcuts, F3 play toggle)
        // --------------------------------------------------------

        // While a key is being bound in Input Settings (and until it is
        // released), the editor shortcuts must not see it.
        lynx::editor::input_settings::Update();

        if (!lynx::editor::input_settings::BlocksEditorShortcuts())
        {
            LYNX_PROFILE_SCOPE("Editor Tick");
            editor::Tick(
                dt
            );
        }


        // --------------------------------------------------------
        // Engine / HRL
        // --------------------------------------------------------
        lynx::gamepad::Poll(editor::BlockGameInput());

        // New lines of the Console : also on the screen of the scene.
        lynx::editor::output_console::FlushToScreen(scene);

        engine->ProgressOneFrame(dt);

        // Requests of the game : Quit stops Play (the editor stays open) ;
        // a hidden cursor is hidden while playing only (EndGame shows it again).
        if (isPlaying && lynx::IsQuitRequested())
        {
            lynx::ClearQuitRequest();
            TogglePlayMode();
        }
        {
            static bool cursor_hidden = false;
            const bool hide = isPlaying && !lynx::IsMouseCursorVisible();
            if (hide != cursor_hidden)
            {
                cursor_hidden = hide;
                SetGameCursorHidden(hide);
            }
        }



        // --------------------------------------------------------
        // Editor UI (Raven) : panels follow the state, the scene image
        // goes to the Viewport, then the window is drawn.
        // --------------------------------------------------------

        {
            LYNX_PROFILE_SCOPE("Editor UI");
            editor::UpdateUi(dt);
        }

        // The presses / clicks of this frame were read (Tick, panels) : the
        // events pumped from here (Rdraw_frame, Rpoll_events) are the next frame's.
        lynx::editor::input::NewFrame();

        {
            // Includes the wait for vsync when it is on.
            LYNX_PROFILE_SCOPE("Draw frame");
            if (!Rdraw_frame())
                editor_quit = true;
        }


        // Screenshots / frame waits asked by commands (the frame is drawn).
        ProcessFrameEndCommands();

        // Splash fade-out over the first frames of the editor.
        lynx::editor::splash::Tick();

        LYNX_PROFILE_FRAME();
    }


    // ------------------------------------------------------------
    // Shutdown
    // ------------------------------------------------------------

    // Closed during the splash fade-out.
    lynx::editor::splash::Close();

    lynx::editor::game_build::Shutdown();
    lynx::editor::project_info::Shutdown();
    lynx::editor::commands_window::Shutdown();
    git_window::Shutdown();
    lynx::editor::script_runner::Shutdown();
    lynx::editor::command_server::Stop();

    // Closed while playing : the game ends first (music...).
    if (isPlaying && gameHooks.on_game_end)
        gameHooks.on_game_end();

    // The module needs a live engine to invalidate its actors / factory.
    gameHooks.Clear();
    gameModule.reset();
    lynx::editor::plugins::UnloadEditorModules();
    lynx::plugins::UnloadAll();

    // Preview scenes of the Particle Editor : before the renderer goes.
    lynx::editor::particle_editor::Shutdown();

    lynx::Engine::Destroy();

    editor::Shutdown();

    lynx::editor::gl::Shutdown();
    StopInterface();


    // "Project > Open another project..." : a new editor, on the browser.
    if (switch_project_requested)
        lynx::host::LaunchEditorProcess({});


    return 0;
}


// =============================================================================
// Voxel World window
// -----------------------------------------------------------------------------
// Size of the voxel world of the current level (its .hrlv). Resizing keeps the
// voxels ; the ones outside the new size are removed (confirmation). Undo :
// Ctrl+Z. Saved with the level (Ctrl+S).
// =============================================================================

// Same settings as after a load : types, render mode, units, rebuild.
static void RefreshVoxelWorldAfterChange()
{
    lynx::voxels::ApplyToScene(scene);
    HRL_SetVoxelRenderMode(scene, appSettings.voxelRenderMode);
    HRL_SetVoxelPhysicalSize(scene, 1.f);
    HRL_BeginVoxelEdit(scene);
    HRL_EndVoxelEdit(scene);
}

static void ResizeVoxelWorldWithUndo(int width, int height)
{
    // Undo : a snapshot of the whole world (compressed .hrlv in memory).
    auto snapshot = std::make_shared<std::vector<uint8_t>>();
    const size_t size = HRL_GetVoxelWorldSaveAllSize(scene);
    if (size > 0)
    {
        snapshot->resize(size);
        snapshot->resize(HRL_SaveVoxelWorldAll(scene, snapshot->data(), snapshot->size()));
    }

    ClearBrushPreview();
    if (!HRL_ResizeVoxelWorld(scene, width, height))
    {
        ShowEditorWarning("The voxel world could not be resized (size too large for the .hrlv format ?)");
        return;
    }
    RefreshVoxelWorldAfterChange();
    editor_dirty = true;

    if (!snapshot->empty())
    {
        PushEditorUndo([snapshot]()
        {
            ClearBrushPreview();
            HRL_LoadVoxelWorldBuffer(scene, snapshot->data(), snapshot->size());
            RefreshVoxelWorldAfterChange();
            editor_dirty = true;
        });
    }
    std::cout << "[WORLD] Voxel world resized to " << width << " x " << height << "\n";
}


// =============================================================================
// Camera Shake window
// -----------------------------------------------------------------------------
// The settings change live ; "Test" shakes the editor camera (the same code as
// the game : CameraShake::Update), "Loop" re-triggers it after a pause. The
// curves show X / Y / roll over the duration (phases of the last trigger).
// Saved in assets/camera_shake.json (loaded at start, editor and game).
// =============================================================================

static bool g_camera_shake_dirty = false;
static bool g_camera_shake_loop = false;
static float g_camera_shake_loop_wait = 0.f;


// Editor camera : the shake started by "Test" (not during Play : the game
// cameras apply it).
static void ApplyCameraShakePreview(HRL_id camera, float dt)
{
    static bool shaking = false;
    lynx::CameraShake& shake = lynx::GetCameraShake();

    if (!isPlaying && shake.active && camera != HRL_INVALID_ID)
    {
        float x = 0.f, y = 0.f, roll = 0.f;
        shake.Update(dt, x, y, roll);
        lynx::camera_state::SetLocation(camera, camX + x, camY + y, camZ);
        if (cameraMode == EditorCameraMode::Mode3D)
            lynx::camera_state::SetRotation(camera, camPitch, camYaw, roll);
        else
            lynx::camera_state::SetRotation(camera, 0.f, -90.f, roll);
        shaking = true;
    }
    else if (shaking)
    {
        shaking = false;
        if (!isPlaying && camera != HRL_INVALID_ID)
        {
            ApplyEditorCameraRotation();
            lynx::camera_state::SetLocation(camera, camX, camY, camZ);
        }
    }
}

// =============================================================================
// Post Process window
// =============================================================================



// =============================================================================
// 2D Lighting window
// =============================================================================



// =============================================================================
// Command palette (Ctrl+P)
// =============================================================================


// =============================================================================
// Interface (Raven) : shell, toolbar, panels, dialogs
// =============================================================================
#include "raven/EditorUi.inl"
