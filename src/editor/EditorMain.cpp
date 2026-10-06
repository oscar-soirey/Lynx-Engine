// =============================================================================
// Lynx Editor
// -----------------------------------------------------------------------------
// Standalone executable of the engine (ImGui panels, gizmo, brush, undo,
// content browser...). It edits a GAME PROJECT chosen at startup :
//
//   LynxEditor.exe                     -> project browser
//   LynxEditor.exe "C:/path/MyGame"    -> opens this project directly
//
// A project is a folder with assets/ and the game DLL in build/ (see
// host/GameProject.h). Once a project is opened, the working directory becomes
// the project root : "assets/...", world.xml, settings.cfg, editor_camera.txt,
// imgui.ini... are the project's files.
//
// Everything specific to a game (voxel types, gameplay camera, collision debug)
// comes from the game DLL through the optional functions of
// core/GameModuleAPI.h.
// =============================================================================

#include "../Lynx.h"
#include "../core/GameModuleAPI.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>   // BeginViewportSideBar (toolbar)
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui_textedit/TextEditor.h>
#include <glfw/glfw3.h>
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
#endif

#include "../audio/AudioCommon.h"
#include "../core/Private/SystemModule.h"
#include "../host/GamepadInput.h"
#include "../gameplay/Private/InputManager.h"

#include "../host/GameProject.h"
#include "InputSettingsEditor.h"
#include "GameBuild.h"
#include "ProjectBrowser.h"
#include "StartupBuild.h"
#include "SplashScreen.h"
#include "commands/CommandRegistry.h"
#include "commands/CommandServer.h"
#include "commands/CommandUtils.h"
#include "commands/CommandsWindow.h"
#include "OutputConsole.h"
#include "ProjectTemplates.h"
#include "LynxieIcon.h"
#include "EditorIcons.h"
#include "PropertyWidgets.h"
#include "ProfilerWindow.h"
#include "ScriptEditors.h"
#include "WidgetEditor.h"
#include "GraphEditors.h"
#include "VoxelTypeEditor.h"
#include "TitleBar.h"
#include "FileIcons.h"
#include "SpriteVoxelizer.h"
#include "WindowIcon.h"
#include "ProjectInfo.h"
#include "EditorFonts.h"
#include "ShipGame.h"
#include "commands/ScriptRunner.h"
#include <array>
#include <chrono>
#include <ctime>

bool isPlaying = false;

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
};

static EditorWindowVisibility editorWindows;

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
         << (editorWindows.nodeGraphTest ? 1 : 0) << '\n';
}

static bool EditorWindowCheckbox(const char* label, bool* value,
                                 lynx::editor::icons::Icon icon = lynx::editor::icons::Icon::Windows)
{
    const bool changed = lynx::editor::icons::Checkbox(label, icon, value);
    if (changed) SaveEditorWindowVisibility();
    return changed;
}



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
    float x = 0.f;          // image position, window pixels
    float y = 0.f;
    float w = 0.f;          // image size, window pixels
    float h = 0.f;
    int renderW = 0;         // HRL scene framebuffer size, physical pixels
    int renderH = 0;
    bool hasRenderSize = false;
};

static SceneViewportState sceneViewport;

// Replaces "!ImGui::GetIO().WantCaptureMouse" for every scene interaction.
static bool SceneAcceptsMouse(bool imgui_wants_mouse)
{
    if (sceneViewport.visible)
        return sceneViewport.hovered;

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
        HRL_SetViewportCamera(viewport, camera);

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



// =============================================================================
// Editor
// =============================================================================


// Set by InitImGui : BeginImGuiFrame checks imgui.ini before its first frame.
static bool default_dock_layout_check = false;

void InitImGui(GLFWwindow* window)
{
    IMGUI_CHECKVERSION();

    ImGui::CreateContext();
    default_dock_layout_check = true;

    ImGuiIO& io = ImGui::GetIO();

    // Tab is reserved by the Lynx editor (Painting / No Painting).
    // Do not let ImGui use Tab for keyboard navigation between widgets.
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;


    // Fonts : fonts/VCR-OSD-MONO.ttf for the editor, fonts/OpenDyslexic-*.otf
    // for Lynxie (see EditorFonts.h).
    lynx::editor::fonts::Load(24.0f);

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(
        window,
        true
    );

    ImGui_ImplOpenGL3_Init(
        "#version 330"
    );
}


// -----------------------------------------------------------------------------
// Default dock layout
// -----------------------------------------------------------------------------
// A project opened for the first time (no imgui.ini, or one without docking
// data) gets the windows docked like this, instead of floating everywhere :
//
//   +-----------------+--------------------------+-------------------------+
//   | Outliner, Paint |                          | Details, Commands,      |
//   |                 |  Viewport (central node, | Color Picking,          |
//   |-----------------|  scripts, Git, Input...) | Camera Shake            |
//   | Content Browser |--------------------------|-------------------------|
//   |                 |  Console, Profiler       | Place Actors            |
//   +-----------------+--------------------------+-------------------------+
//
// The windows that are not visible yet are docked there when they first open.
static bool default_dock_layout_pending = false;

// Central node of the dockspace (Viewport) : the script editors open there.
static ImGuiID central_dock_id = 0;

static bool ImGuiIniHasDockLayout(const char* ini_file)
{
    if (!ini_file)
        return true;   // no ini at all : nothing to restore, nothing to build

    std::ifstream file(ini_file);
    if (!file)
        return false;

    std::string line;
    while (std::getline(file, line))
    {
        if (line.rfind("[Docking][Data]", 0) == 0)
            return true;
    }
    return false;
}

static void BuildDefaultDockLayout(ImGuiID dockspace_id)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(
        dockspace_id,
        static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) |
            ImGuiDockNodeFlags_PassthruCentralNode
    );
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

    // Proportions of the Blocky layout (3309 x 1360 work area).
    ImGuiID center = dockspace_id;
    const ImGuiID right =
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.257f, nullptr, &center);
    const ImGuiID left =
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.235f, nullptr, &center);

    ImGuiID left_top = left;
    const ImGuiID left_bottom =
        ImGui::DockBuilderSplitNode(left_top, ImGuiDir_Down, 0.476f, nullptr, &left_top);

    ImGuiID right_top = right;
    const ImGuiID right_bottom =
        ImGui::DockBuilderSplitNode(right_top, ImGuiDir_Down, 0.30f, nullptr, &right_top);

    ImGuiID center_top = center;
    const ImGuiID center_bottom =
        ImGui::DockBuilderSplitNode(center_top, ImGuiDir_Down, 0.345f, nullptr, &center_top);

    // Order = order of the tabs ; the first one is the visible tab.
    ImGui::DockBuilderDockWindow("Outliner", left_top);
    ImGui::DockBuilderDockWindow("Paint", left_top);
    ImGui::DockBuilderDockWindow("Content Browser", left_bottom);

    ImGui::DockBuilderDockWindow("Viewport", center_top);
    ImGui::DockBuilderDockWindow("Input Settings", center_top);
    ImGui::DockBuilderDockWindow("Git", center_top);

    ImGui::DockBuilderDockWindow("Console", center_bottom);
    ImGui::DockBuilderDockWindow("Profiler", center_bottom);

    ImGui::DockBuilderDockWindow("Details", right_top);
    ImGui::DockBuilderDockWindow("Commands", right_top);
    ImGui::DockBuilderDockWindow("Color Picking", right_top);
    ImGui::DockBuilderDockWindow("Camera Shake", right_top);
    ImGui::DockBuilderDockWindow("Place Actors", right_bottom);

    ImGui::DockBuilderFinish(dockspace_id);
}


void BeginImGuiFrame()
{
    // Before the first NewFrame (which reads imgui.ini) : no saved layout ->
    // the default one is built.
    if (default_dock_layout_check)
    {
        default_dock_layout_check = false;
        default_dock_layout_pending =
            !ImGuiIniHasDockLayout(ImGui::GetIO().IniFilename);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Main dockspace: every editor panel can be docked/rearranged by ImGui.
    const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(
        0,
        ImGui::GetMainViewport(),
        ImGuiDockNodeFlags_PassthruCentralNode
    );

    if (default_dock_layout_pending)
    {
        default_dock_layout_pending = false;
        BuildDefaultDockLayout(dockspace_id);
    }

    ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockspace_id);
    central_dock_id = central ? central->ID : 0;
}


void EndImGuiFrame()
{
    LYNX_PROFILE_SCOPE("ImGui render");

    ImGui::Render();

    ImGui_ImplOpenGL3_RenderDrawData(
        ImGui::GetDrawData()
    );
}


void ShutdownImGui()
{
    lynx::editor::graph_editors::Shutdown();     // Anim Graph / Behavior Tree editors (ImNodes)
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}


float camX;
float camY;
float camZ = 600.f;   // voxel units (1 = 1 voxel)

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


void SetActorSelected(lynx::Actor* actor)
{
    editing_actor = actor;
    editing_object = HRL_INVALID_ID;

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

static HRL_id GetActorTypePreviewTexture(
    const std::string& type_name)
{
    if (auto it = actorPreviewTextures.find(type_name);
        it != actorPreviewTextures.end())
    {
        return it->second;
    }

    if (actorPreviewSearchDone.find(type_name) !=
        actorPreviewSearchDone.end())
    {
        return HRL_INVALID_ID;
    }

    actorPreviewSearchDone.insert(type_name);

    const std::filesystem::path root =
        std::filesystem::current_path() / "assets";

    std::error_code error;

    if (!std::filesystem::exists(root, error))
        return HRL_INVALID_ID;

    std::string wanted = type_name;

    std::transform(
        wanted.begin(),
        wanted.end(),
        wanted.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        }
    );

    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(
             root,
             std::filesystem::directory_options::skip_permission_denied,
             error))
    {
        if (error)
            break;

        if (!entry.is_regular_file(error) ||
            !IsPreviewImageExtension(entry.path()))
        {
            continue;
        }

        std::string stem =
            entry.path().stem().string();

        std::transform(
            stem.begin(),
            stem.end(),
            stem.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            }
        );

        if (stem != wanted)
            continue;

        const auto data =
            lynx::fs::ReadBinary(
                entry.path().string()
            );

        if (data.empty())
            continue;

        const HRL_id texture =
            HRL_CreateTexture(
                reinterpret_cast<const char*>(data.data()),
                data.size()
            );

        if (texture != HRL_INVALID_ID)
        {
            actorPreviewTextures[type_name] =
                texture;

            return texture;
        }
    }

    return HRL_INVALID_ID;
}

// "color", "light_color", "tintColor"... : drawn as a color picker in Details.
// Actor::auto_possess_player : "Disabled" (-1), "Player 0".."Player 7",
// like AutoPossessPlayer in Unreal.
static bool DrawAutoPossessCombo(int& value)
{
    constexpr int kMaxPlayers = 8;

    char preview[32];
    if (value < 0)
        std::snprintf(preview, sizeof(preview), "Disabled");
    else
        std::snprintf(preview, sizeof(preview), "Player %d", value);

    bool changed = false;

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##value", preview))
    {
        if (ImGui::Selectable("Disabled", value < 0))
        {
            changed = value != -1;
            value = -1;
        }

        for (int i = 0; i < std::max(kMaxPlayers, value + 1); ++i)
        {
            char label[32];
            std::snprintf(label, sizeof(label), "Player %d", i);

            if (ImGui::Selectable(label, value == i))
            {
                changed = value != i;
                value = i;
            }
        }

        ImGui::EndCombo();
    }

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Player that possesses this actor when the game starts\n"
                          "(player 0 = default player, others : Engine::CreatePlayer).");

    return changed;
}

static bool IsColorPropertyName(const std::string& name)
{
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("color") != std::string::npos || lower.find("colour") != std::string::npos ||
           lower.find("tint") != std::string::npos;
}

static void DrawActorTypePreview(
    const std::string& type_name,
    float size)
{
    // Engine classes (lights, SpriteActor...) : their icon.
    {
        uint32_t texture = HRL_INVALID_ID;
        float uv[4] = {};
        if (lynx::GetEngineActorIcon(type_name, texture, uv))
        {
            const unsigned int gl_texture = HRL_GL_GetTextureGL_ID(texture);
            if (gl_texture != 0)
            {
                // HRL textures are bottom-up : v swapped.
                ImGui::Image((ImTextureID)(intptr_t)gl_texture, ImVec2(size, size),
                             ImVec2(uv[0], uv[3]), ImVec2(uv[2], uv[1]));
                return;
            }
        }
    }

    const HRL_id texture =
        GetActorTypePreviewTexture(
            type_name
        );

    if (texture != HRL_INVALID_ID)
    {
        const unsigned int gl_texture =
            HRL_GL_GetTextureGL_ID(texture);

        if (gl_texture != 0)
        {
            ImGui::Image(
                (ImTextureID)(intptr_t)gl_texture,
                ImVec2(size, size),
                ImVec2(0.f, 1.f),
                ImVec2(1.f, 0.f)
            );

            return;
        }
    }

    // Fallback icon when the actor has no image named after its class.
    uint32_t hash = 2166136261u;

    for (unsigned char c : type_name)
    {
        hash ^= c;
        hash *= 16777619u;
    }

    const ImVec2 min =
        ImGui::GetCursorScreenPos();

    const ImVec2 max(
        min.x + size,
        min.y + size
    );

    ImGui::GetWindowDrawList()->AddRectFilled(
        min,
        max,
        IM_COL32(
            static_cast<int>(80u + (hash & 0x7Fu)),
            static_cast<int>(80u + ((hash >> 8) & 0x7Fu)),
            static_cast<int>(80u + ((hash >> 16) & 0x7Fu)),
            255
        ),
        5.f
    );

    char letter[2] =
    {
        type_name.empty() ? '?' : type_name.front(),
        '\0'
    };

    const ImVec2 text_size =
        ImGui::CalcTextSize(letter);

    ImGui::GetWindowDrawList()->AddText(
        ImVec2(
            min.x + (size - text_size.x) * 0.5f,
            min.y + (size - text_size.y) * 0.5f
        ),
        IM_COL32(255, 255, 255, 255),
        letter
    );

    ImGui::Dummy(
        ImVec2(size, size)
    );
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

    glfwGetCursorPos(
        glfwGetCurrentContext(),
        &mouse_x,
        &mouse_y
    );

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

static void HandleActorPlacementDrag(
    lynx::Level* level)
{
    if (!level || !actor_drag_active)
        return;

    // Keep the actual actor under the cursor, including while the cursor is
    // moving across the scene. MouseToScene() falls back to raw window
    // coordinates because the optional ImGui Viewport window is disabled.
    UpdateActorLocationFromMouse(
        actor_drag_preview
    );

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        // A release over the scene commits the actor. The scene viewport is
        // itself an ImGui window, so WantCaptureMouse alone would cancel drops
        // there just like drops over an editor panel.
        // WantCaptureMouse can remain true because the Place Actors row is
        // still the active drag source, even after the cursor has left that
        // window. Determine the release target from the current hovered item
        // or window instead.
        const bool released_over_scene = sceneViewport.visible
            ? sceneViewport.hovered
            : !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);

        if (released_over_scene)
            CommitActorPlacementDrag();
        else
            CancelActorPlacementDrag();
    }
}

void PlaceActorsWindow(lynx::Level* level)
{
    if (!level)
        return;

    ImGui::Begin("Place Actors");

    static char search_buffer[256] = {};

    ImGui::InputTextWithHint(
        "##PlaceActorSearch",
        "Search actors...",
        search_buffer,
        sizeof(search_buffer)
    );

    ImGui::Separator();

    auto* factory =
        lynx::Engine::Get()
            ->GetFactory()
            .GetInternalFactory();

    if (!factory)
    {
        ImGui::TextDisabled("Factory unavailable");
        ImGui::End();
        return;
    }

    std::vector<std::string> actor_types;
    actor_types.reserve(factory->size());

    for (const auto& [type_name, constructor] : *factory)
    {
        (void)constructor;

        if (type_name.find(search_buffer) == std::string::npos)
            continue;

        actor_types.push_back(type_name);
    }

    std::sort(
        actor_types.begin(),
        actor_types.end()
    );

    // Classes of the engine (Actor, lights...) first, then the game's.
    std::stable_partition(
        actor_types.begin(),
        actor_types.end(),
        [](const std::string& name) { return lynx::IsEngineActorClass(name); }
    );

    if (actor_types.empty())
    {
        ImGui::TextDisabled("No matching actors");
    }
    else
    {
        int section = -1;   // 0 : engine, 1 : game

        for (const std::string& type_name : actor_types)
        {
            const int type_section = lynx::IsEngineActorClass(type_name) ? 0 : 1;
            if (type_section != section)
            {
                section = type_section;
                ImGui::SeparatorText(section == 0 ? "Engine" : "Game");
            }

            ImGui::PushID(type_name.c_str());

            const float preview_size = 42.f;

            ImGui::BeginGroup();

            DrawActorTypePreview(
                type_name,
                preview_size
            );

            ImGui::SameLine();

            if (ImGui::Button(
                    type_name.c_str(),
                    ImVec2(
                        std::max(
                            1.f,
                            ImGui::GetContentRegionAvail().x
                        ),
                        preview_size
                    )
                ))
            {
                // Keep the existing click-to-place behaviour.
                lynx::Actor* actor =
                    level->SpawnActor(
                        type_name.c_str()
                    );

                if (actor)
                {
                    AssignDefaultActorName(
                        level,
                        actor,
                        type_name
                    );

                    const std::string actor_id =
                        actor->object_id_;

                    UpdateActorLocationFromMouse(
                        actor
                    );

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

                    SetActorSelected(actor);
                    MarkEditorDirty();
                }
            }

            // Dragging the row creates a real temporary actor immediately.
            // The instance then follows the mouse until it is released.
            if (ImGui::BeginDragDropSource(
                    ImGuiDragDropFlags_SourceAllowNullID
                ))
            {
                ImGui::SetDragDropPayload(
                    "LYNX_ACTOR_TYPE",
                    type_name.c_str(),
                    type_name.size() + 1
                );

                if (!actor_drag_active)
                {
                    BeginActorPlacementDrag(
                        level,
                        type_name
                    );
                }

                ImGui::BeginGroup();

                ImGui::Text(
                    "Placing %s",
                    type_name.c_str()
                );

                ImGui::TextDisabled(
                    "Release in the scene"
                );

                ImGui::EndGroup();

                ImGui::EndDragDropSource();
            }

            ImGui::EndGroup();

            ImGui::Separator();
            ImGui::PopID();
        }
    }

    ImGui::End();
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


    ClearBrushPreview();

    std::cout
        << "[SAVE] Saving voxel world to: "
        << world_file_path_string
        << "\n";

    HRL_SaveVoxelWorldAllFile(
        scene,
        world_file_path_string.c_str()
    );

    editor_level->SaveToFile("world.xml");

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

    glfwGetCursorPos(
        glfwGetCurrentContext(),
        &mouse_x,
        &mouse_y
    );

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


static void DropCallback(
    GLFWwindow* window,
    int count,
    const char** paths)
{
    if (!window)
        return;

    pending_explorer_drop.paths.clear();

    glfwGetCursorPos(
        window,
        &pending_explorer_drop.mouse_x,
        &pending_explorer_drop.mouse_y
    );

    for (int i = 0; i < count; ++i)
    {
        if (paths[i])
            pending_explorer_drop.paths.emplace_back(paths[i]);
    }
}



// Script editors (one window per file) : ScriptEditors.h / .cpp.


// Right click menus, rename / duplicate / delete / new file... of the browser.
#include "ContentBrowserActions.inl"

// Git window (status, commit, pull / push, history).
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

// Actor menu : "Move to" > folders.
static void OutlinerMoveToMenu(lynx::Level* level, lynx::Actor* actor)
{
    if (!ImGui::BeginMenu("Move to folder"))
        return;
    if (ImGui::MenuItem("(root)", nullptr, actor->outliner_folder.empty()))
        OutlinerMoveActor(level, actor, "");
    for (const std::string& f : OutlinerAllFolders(level))
        if (ImGui::MenuItem(f.c_str(), nullptr, actor->outliner_folder == f))
            OutlinerMoveActor(level, actor, f);
    ImGui::Separator();
    if (ImGui::MenuItem("New folder"))
    {
        PushOutlinerUndo(level);
        const std::string folder = OutlinerUniqueChild(level, actor->outliner_folder, "New Folder");
        OutlinerDeclare(level, folder);
        actor->outliner_folder = folder;
        MarkEditorDirty();
        OutlinerStartRename(folder);
    }
    ImGui::EndMenu();
}

// Drop target of a folder ("" = root) : actors and folders.
static void OutlinerDropTarget(lynx::Level* level, const std::string& folder)
{
    if (!ImGui::BeginDragDropTarget())
        return;
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("LYNX_OUTLINER_ACTOR"))
    {
        lynx::Actor* actor = *static_cast<lynx::Actor* const*>(p->Data);
        const auto& alive = level->GetActors();
        if (std::find(alive.begin(), alive.end(), actor) != alive.end())
            OutlinerMoveActor(level, actor, folder);
    }
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("LYNX_OUTLINER_FOLDER"))
    {
        const std::string dragged(static_cast<const char*>(p->Data));
        if (!OutlinerInside(folder, dragged) && OutlinerParent(dragged) != folder)
        {
            PushOutlinerUndo(level);
            OutlinerRenameFolder(level, dragged, OutlinerUniqueChild(level, folder, OutlinerName(dragged)));
            MarkEditorDirty();
        }
    }
    ImGui::EndDragDropTarget();
}

// The tree : "+ Folder", then the folders (open / closed) and the actors.
template <class DrawActor>
static void DrawOutlinerTree(lynx::Level* level, DrawActor& draw_actor)
{
    const auto& actors = level->GetActors();
    const std::set<std::string> folders = OutlinerAllFolders(level);

    if (ImGui::SmallButton("+ Folder"))
        OutlinerNewFolder(level, "");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("New folder (drag actors onto a folder to put them in)");
    ImGui::SameLine();
    ImGui::TextDisabled("%d actor%s", static_cast<int>(actors.size()), actors.size() == 1 ? "" : "s");
    ImGui::Separator();

    // Children of each folder (sorted by std::set), actors of each folder.
    std::map<std::string, std::vector<std::string>> children;
    for (const std::string& f : folders)
        children[OutlinerParent(f)].push_back(f);
    std::map<std::string, std::vector<size_t>> by_folder;
    for (size_t i = 0; i < actors.size(); ++i)
        if (actors[i])
            by_folder[actors[i]->outliner_folder].push_back(i);

    const float icon = ImGui::GetTextLineHeight();
    std::function<void(const std::string&)> draw = [&](const std::string& folder)
    {
        for (const std::string& child : children[folder])
        {
            ImGui::PushID(child.c_str());
            const std::string label = std::string(static_cast<size_t>(icon / ImGui::CalcTextSize(" ").x) + 1, ' ') +
                                      OutlinerName(child);
            const bool open = ImGui::TreeNodeEx("##folder", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen |
                                                ImGuiTreeNodeFlags_OpenOnDoubleClick, "%s", label.c_str());
            // Folder icon after the arrow
            {
                const ImVec2 min = ImGui::GetItemRectMin();
                lynx::editor::file_icons::DrawAt(ImGui::GetWindowDrawList(), lynx::editor::file_icons::Kind::Folder,
                                                 ImVec2(min.x + ImGui::GetTreeNodeToLabelSpacing(), min.y +
                                                        (ImGui::GetItemRectSize().y - icon) * 0.5f), icon);
            }
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("LYNX_OUTLINER_FOLDER", child.c_str(), child.size() + 1);
                ImGui::TextUnformatted(child.c_str());
                ImGui::EndDragDropSource();
            }
            OutlinerDropTarget(level, child);
            if (ImGui::BeginPopupContextItem("##folder_menu"))
            {
                if (ImGui::MenuItem("New subfolder"))
                    OutlinerNewFolder(level, child);
                if (ImGui::MenuItem("Rename", "F2"))
                    OutlinerStartRename(child);
                if (ImGui::MenuItem("Delete folder"))
                {
                    PushOutlinerUndo(level);
                    OutlinerDeleteFolder(level, child);
                    MarkEditorDirty();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("The actors inside go to the parent folder.");
                ImGui::EndPopup();
            }
            if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_F2, false))
                OutlinerStartRename(child);
            if (open)
            {
                draw(child);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        for (size_t i : by_folder[folder])
        {
            ImGui::PushID(static_cast<int>(i));
            draw_actor(actors[i], i);
            ImGui::PopID();
        }
    };

    if (actors.empty() && folders.empty())
        ImGui::TextDisabled("No actors");
    draw("");

    // The free space below : drop here = back to the root.
    const ImVec2 rest = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##outliner_root", ImVec2(std::max(1.f, rest.x), std::max(ImGui::GetTextLineHeight(), rest.y)));
    OutlinerDropTarget(level, "");
    if (ImGui::BeginPopupContextItem("##outliner_root_menu"))
    {
        if (ImGui::MenuItem("New folder"))
            OutlinerNewFolder(level, "");
        ImGui::EndPopup();
    }

    // Rename popup
    if (outliner_rename_open)
    {
        ImGui::OpenPopup("Rename folder");
        outliner_rename_open = false;
    }
    if (ImGui::BeginPopupModal("Rename folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##name", outliner_rename_buffer, sizeof(outliner_rename_buffer),
                                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        std::string name = outliner_rename_buffer;
        name.erase(std::remove(name.begin(), name.end(), '/'), name.end());
        name.erase(std::remove(name.begin(), name.end(), '|'), name.end());
        const std::string parent = OutlinerParent(outliner_rename_path);
        const std::string target = parent.empty() ? name : parent + "/" + name;
        const bool taken = target != outliner_rename_path && OutlinerAllFolders(level).count(target) > 0;
        if (taken)
            ImGui::TextDisabled("A folder \"%s\" already exists", name.c_str());
        ImGui::BeginDisabled(name.empty() || taken);
        if (ImGui::Button("OK") || (enter && !name.empty() && !taken))
        {
            if (target != outliner_rename_path)
            {
                PushOutlinerUndo(level);
                OutlinerRenameFolder(level, outliner_rename_path, target);
                MarkEditorDirty();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
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

// Header of the Content Browser :
//
//    (Lynxie)  [Assets] / maps / caves                       12 items
//     /\_/\    [^] [search............................] [x]  [+ New]
//  ---(   )---------------------------------------------------------- folders
//
// Lynxie stands on the line above the folders (click : opens her chat).
static void DrawContentBrowserHeader(const std::filesystem::path& relative_current)
{
    namespace cba = content_browser_actions;
    namespace icons = lynx::editor::icons;

    ImGui::SetWindowFontScale(0.8f);

    const ImGuiStyle& style = ImGui::GetStyle();
    const float row_h = ImGui::GetFrameHeight();
    const float header_h = row_h * 2.f + style.ItemSpacing.y;
    // Lynxie is taller than the two rows : the rows sit at her feet, on the line.
    const float lynxie_h = std::floor((header_h + style.ItemSpacing.y) * 1.45f);
    const float lynxie_w = lynx::editor::lynxie_icon::WidthFor(lynxie_h);
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const float line_y = top.y + lynxie_h;
    const ImVec2 start(top.x, line_y - header_h - style.ItemSpacing.y * 0.5f - 1.f);

    // Lynxie (her feet on the line)
    ImGui::SetCursorScreenPos(ImVec2(start.x, line_y - lynxie_h));
    if (ImGui::InvisibleButton("##BrowserLynxie", ImVec2(lynxie_w, lynxie_h)))
    {
        editorWindows.commands = true;
        SaveEditorWindowVisibility();
        lynx::editor::commands_window::ShowLynxie();
    }
    {
        const bool hovered = ImGui::IsItemHovered();
        ImVec4 tint = style.Colors[ImGuiCol_Text];
        if (!hovered)
            tint.w *= 0.85f;
        const float hop = hovered ? -2.f : 0.f;   // a little jump when hovered
        lynx::editor::lynxie_icon::DrawAt(ImGui::GetWindowDrawList(),
                                         ImVec2(start.x, line_y - lynxie_h + hop), lynxie_h,
                                         ImGui::ColorConvertFloat4ToU32(tint));
        if (hovered)
            ImGui::SetTooltip("Lynxie : ask about your assets, the engine, the level...");
    }

    const float left = start.x + lynxie_w + style.ItemSpacing.x * 2.f;

    // Row 1 : path
    ImGui::SetCursorScreenPos(ImVec2(left, start.y));
    if (icons::ButtonWithLabel("Assets##BrowserRoot", icons::Icon::Folder))
        content_browser_current_path = content_browser_root;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", content_browser_root.generic_string().c_str());

    std::filesystem::path breadcrumb_path;
    int breadcrumb_index = 0;
    for (const auto& part : relative_current)
    {
        if (part.empty() || part == ".")
            continue;
        breadcrumb_path /= part;
        ImGui::SameLine(0.f, style.ItemSpacing.x * 0.5f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("/");
        ImGui::SameLine(0.f, style.ItemSpacing.x * 0.5f);

        const std::string label = part.string() + "##Breadcrumb" + std::to_string(breadcrumb_index++);
        if (ImGui::Button(label.c_str()))
            content_browser_current_path = content_browser_root / breadcrumb_path;
    }

    // Number of items, on the right
    {
        char count[32];
        std::snprintf(count, sizeof(count), "%d item%s", content_browser_item_count,
                      content_browser_item_count == 1 ? "" : "s");
        const float w = ImGui::CalcTextSize(count).x;
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        if (ImGui::GetItemRectMax().x + style.ItemSpacing.x * 2.f + w < right)
        {
            ImGui::SameLine(right - w - ImGui::GetWindowPos().x);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", count);
        }
    }

    // Row 2 : up, search, new
    ImGui::SetCursorScreenPos(ImVec2(left, start.y + row_h + style.ItemSpacing.y));
    ImGui::BeginDisabled(content_browser_back.empty());
    if (ImGui::ArrowButton("##BrowserBack", ImGuiDir_Left))
        ContentBrowserGo(-1);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Back (mouse back button)");
    ImGui::SameLine(0.f, 2.f);
    ImGui::BeginDisabled(content_browser_forward.empty());
    if (ImGui::ArrowButton("##BrowserForward", ImGuiDir_Right))
        ContentBrowserGo(+1);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Forward (mouse forward button)");
    ImGui::SameLine(0.f, 2.f);

    const bool at_root = content_browser_current_path == content_browser_root;
    ImGui::BeginDisabled(at_root);
    if (ImGui::ArrowButton("##BrowserUp", ImGuiDir_Up))
    {
        const std::filesystem::path parent = content_browser_current_path.parent_path();
        if (IsPathInside(parent, content_browser_root))
            content_browser_current_path = parent;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Parent folder");

    ImGui::SameLine();
    const float new_w = ImGui::CalcTextSize("+ New").x + style.FramePadding.x * 2.f;
    const float clear_w = content_browser_search[0] ? row_h + style.ItemSpacing.x : 0.f;
    const float search_w = std::max(60.f, ImGui::GetContentRegionAvail().x - new_w - clear_w - style.ItemSpacing.x);
    ImGui::AlignTextToFramePadding();
    icons::Draw(icons::Icon::Search);
    ImGui::SameLine(0.f, style.ItemSpacing.x * 0.5f);
    ImGui::SetNextItemWidth(search_w - ImGui::GetFontSize() - style.ItemSpacing.x * 0.5f);
    ImGui::InputTextWithHint("##BrowserSearch", "Search in this folder", content_browser_search,
                             sizeof(content_browser_search));
    if (content_browser_search[0])
    {
        ImGui::SameLine();
        if (icons::Button("##BrowserClearSearch", icons::Icon::Clear))
            content_browser_search[0] = 0;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Clear the search");
    }
    ImGui::SameLine();
    if (ImGui::Button("+ New"))
        ImGui::OpenPopup("##BrowserNew");
    if (ImGui::BeginPopup("##BrowserNew"))
    {
        cba::NewFileMenu(content_browser_current_path);
        ImGui::EndPopup();
    }

    // The line Lynxie stands on
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float x0 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x;
    const float x1 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    dl->AddLine(ImVec2(x0, line_y), ImVec2(x1, line_y), ImGui::GetColorU32(ImGuiCol_Separator), 2.f);

    ImGui::SetCursorScreenPos(ImVec2(start.x, line_y + style.ItemSpacing.y));
    ImGui::Dummy(ImVec2(1.f, 0.f));
    ImGui::SetWindowFontScale(0.62f);
}

// One cell of the Content Browser grid (drawn over its invisible button).
static void DrawAssetTile(ImDrawList* draw_list, const ImVec2& cell_min, const ImVec2& cell_max,
                          const ImVec2& thumb_min, float image_size, float item_width,
                          bool hovered, bool active, bool selected,
                          const std::filesystem::path& path, bool is_directory, const std::string& filename)
{
        // Tile : nothing at rest (the window background shows through), a
        // soft highlight when hovered / pressed, the selection color when selected.
        if (hovered || active || selected)
        {
            ImVec4 color = ImGui::GetStyleColorVec4(
                active   ? ImGuiCol_HeaderActive :
                selected ? ImGuiCol_Header :
                           ImGuiCol_HeaderHovered);
            if (!selected && !active)
                color.w *= 0.55f;

            draw_list->AddRectFilled(cell_min, cell_max, ImGui::ColorConvertFloat4ToU32(color),
                                     ImGui::GetStyle().FrameRounding);
            if (selected)
                draw_list->AddRect(cell_min, cell_max, ImGui::GetColorU32(ImGuiCol_HeaderActive),
                                   ImGui::GetStyle().FrameRounding, 0, 2.f);
        }

        // Icon of the asset type (colored pixel art, 32 px : drawn at x2).
        {
            const lynx::editor::file_icons::Kind kind =
                lynx::editor::file_icons::ForFile(path, is_directory);

            const float icon_size =
                std::max(32.f, std::floor(image_size * 0.8f / 32.f) * 32.f);

            // A small hop when hovered.
            const float hop = hovered && !active ? -2.f : 0.f;

            lynx::editor::file_icons::DrawAt(
                draw_list,
                kind,
                ImVec2(
                    std::floor(thumb_min.x + (image_size - icon_size) * 0.5f),
                    std::floor(thumb_min.y + (image_size - icon_size) * 0.5f + hop)
                ),
                icon_size
            );

            if (hovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                ImGui::SetItemTooltip("%s\n%s", filename.c_str(), lynx::editor::file_icons::Describe(kind));
        }

        // Asset name under the image, clipped/wrapped to the cell width.
        const ImVec2 text_size =
            ImGui::CalcTextSize(filename.c_str(), nullptr, false, item_width - 8.f);

        const float text_x =
            cell_min.x + (item_width - std::min(text_size.x, item_width - 8.f)) * 0.5f;

        draw_list->AddText(
            ImGui::GetFont(),
            ImGui::GetFontSize(),
            ImVec2(text_x, cell_min.y + image_size + 9.f),
            ImGui::GetColorU32(ImGuiCol_Text),
            filename.c_str(),
            nullptr,
            item_width - 8.f
        );

}

// Back / forward buttons of the mouse over the Content Browser (call inside it).
static void ContentBrowserMouseHistory()
{
    ContentBrowserTrackHistory();
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
    {
        if (ImGui::IsMouseClicked(3))
            ContentBrowserGo(-1);
        if (ImGui::IsMouseClicked(4))
            ContentBrowserGo(+1);
    }
}

static void DrawContentBrowser(lynx::Level* level)
{
    namespace cba = content_browser_actions;

    content_browser_focused = false;

    if (!level)
        return;

    ImGui::Begin("Content Browser");

    content_browser_focused =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // History : folder changes of the last frame, then the side buttons of the
    // mouse (back = button 4, forward = button 5) over the window.
    ContentBrowserMouseHistory();
    ImGui::SetWindowFontScale(0.62f);

    const ImVec2 window_pos =
        ImGui::GetWindowPos();

    const ImVec2 window_size =
        ImGui::GetWindowSize();

    const bool native_drop_inside =
        !pending_explorer_drop.paths.empty() &&
        pending_explorer_drop.mouse_x >= window_pos.x &&
        pending_explorer_drop.mouse_x <= window_pos.x + window_size.x &&
        pending_explorer_drop.mouse_y >= window_pos.y &&
        pending_explorer_drop.mouse_y <= window_pos.y + window_size.y;

    if (native_drop_inside)
    {
        CopyExplorerFilesToContentBrowser();
    }
    else if (!pending_explorer_drop.paths.empty())
    {
        pending_explorer_drop.paths.clear();
    }

    std::filesystem::path relative_current;

    if (IsPathInside(
            content_browser_current_path,
            content_browser_root))
    {
        std::error_code error;

        relative_current =
            std::filesystem::relative(
                content_browser_current_path,
                content_browser_root,
                error
            );

        if (error)
            relative_current.clear();
    }

    // Header : Lynxie on the line above the folders, path, search, New.
    DrawContentBrowserHeader(relative_current);

    // The folder is listed (and sorted) only when it changes, or twice a
    // second : listing a big folder every frame (assets/ with hundreds of
    // files) made the editor drop from ~280 to ~60 fps.
    static std::filesystem::path listed_path;
    static double listed_time = -1.0;
    static std::vector<std::filesystem::directory_entry> entries;

    // An action of this window (rename, delete, paste, new file... : a click
    // or a key) : listed again at once instead of after half a second.
    const bool browser_action =
        content_browser_focused &&
        (ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
         ImGui::IsMouseReleased(ImGuiMouseButton_Right) ||
         ImGui::IsKeyReleased(ImGuiKey_Enter) ||
         ImGui::IsKeyReleased(ImGuiKey_Delete) ||
         ImGui::IsKeyReleased(ImGuiKey_V) ||
         ImGui::IsKeyReleased(ImGuiKey_D));

    if (listed_path != content_browser_current_path ||
        browser_action ||
        ImGui::GetTime() - listed_time > 0.5)
    {
        listed_path = content_browser_current_path;
        listed_time = ImGui::GetTime();
        entries.clear();

        std::error_code error;

        if (std::filesystem::exists(
                content_browser_current_path,
                error))
        {
            for (const auto& entry :
                 std::filesystem::directory_iterator(
                     content_browser_current_path,
                     std::filesystem::directory_options::skip_permission_denied,
                     error))
            {
                if (error)
                    break;

                entries.push_back(entry);
            }
        }

        std::vector<std::pair<bool, std::string>> keys;
        std::vector<size_t> order(entries.size());
        for (size_t i = 0; i < entries.size(); ++i)
        {
            std::error_code dir_error;
            keys.emplace_back(entries[i].is_directory(dir_error), entries[i].path().filename().string());
            order[i] = i;
        }

        std::sort(
            order.begin(),
            order.end(),
            [&keys](size_t a, size_t b)
            {
                if (keys[a].first != keys[b].first)
                    return keys[a].first > keys[b].first;
                return keys[a].second < keys[b].second;
            }
        );

        std::vector<std::filesystem::directory_entry> sorted;
        sorted.reserve(entries.size());
        for (size_t i : order)
            sorted.push_back(entries[i]);
        entries.swap(sorted);
    }

    // Search : the entries whose name contains the text (any case).
    std::vector<const std::filesystem::directory_entry*> shown;
    shown.reserve(entries.size());
    {
        std::string needle = content_browser_search;
        std::transform(needle.begin(), needle.end(), needle.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (const auto& entry : entries)
        {
            if (!needle.empty())
            {
                std::string name = entry.path().filename().string();
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (name.find(needle) == std::string::npos)
                    continue;
            }
            shown.push_back(&entry);
        }
    }
    content_browser_item_count = static_cast<int>(shown.size());

    // The grid scrolls under the header.
    ImGui::BeginChild("##ContentGrid", ImVec2(0.f, 0.f), ImGuiChildFlags_None);
    ImGui::SetWindowFontScale(0.62f);
    content_browser_focused = content_browser_focused ||
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // --------------------------------------------------------
    // Asset grid
    // --------------------------------------------------------

    constexpr float item_width = 128.f;
    constexpr float item_height = 126.f;
    constexpr float image_size = 92.f;
    constexpr float item_spacing = 10.f;

    const float content_width =
        std::max(1.f, ImGui::GetContentRegionAvail().x);

    const int columns =
        std::max(
            1,
            static_cast<int>(
                (content_width + item_spacing) /
                (item_width + item_spacing)
            )
        );

    // Only the visible rows are drawn (a folder can hold hundreds of files).
    const int row_count =
        (static_cast<int>(shown.size()) + columns - 1) / columns;

    ImGuiListClipper clipper;
    clipper.Begin(row_count, item_height + ImGui::GetStyle().ItemSpacing.y);

    while (clipper.Step())
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
    for (int column = 0; column < columns; ++column)
    {
        const size_t index = static_cast<size_t>(row) * static_cast<size_t>(columns) + static_cast<size_t>(column);
        if (index >= shown.size())
            break;

        const std::filesystem::directory_entry& entry = *shown[index];

        if (column > 0)
            ImGui::SameLine(0.f, item_spacing);

        ImGui::PushID(entry.path().generic_string().c_str());

        const bool is_directory =
            entry.is_directory();

        const std::string filename =
            entry.path().filename().string();

        // Keep the whole cell clickable, but draw the actual asset
        // thumbnail ourselves so the button behaves like an image button.
        const ImVec2 cell_pos = ImGui::GetCursorScreenPos();
        const bool clicked =
            ImGui::InvisibleButton(
                "##Asset",
                ImVec2(item_width, item_height)
            );

        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const bool selected = cba::state.selected == entry.path();

        // Double-click on a text file (.js, .py, .json, .xml, .txt...) : opens
        // it in a script editor window (one window per file). A single click
        // keeps the existing behaviour, files stay usable as drag sources.
        if (!is_directory &&
            hovered &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            // .widget : Widget Editor (UMG-like designer).
            if (lynx::editor::widget_editor::CanOpen(entry.path()))
                lynx::editor::widget_editor::Open(entry.path());
            // .animgraph / .bt : node editors.
            else if (lynx::editor::graph_editors::CanOpen(entry.path()))
                lynx::editor::graph_editors::Open(entry.path());
            else if (lynx::editor::script_editors::CanOpen(entry.path()))
                lynx::editor::script_editors::Open(entry.path());
        }

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        const ImVec2 cell_min = cell_pos;
        const ImVec2 cell_max(
            cell_pos.x + item_width,
            cell_pos.y + item_height
        );

        const float thumb_x =
            cell_pos.x + (item_width - image_size) * 0.5f;
        const float thumb_y =
            cell_pos.y + 4.f;

        const ImVec2 thumb_min(thumb_x, thumb_y);
        const ImVec2 thumb_max(
            thumb_x + image_size,
            thumb_y + image_size
        );
        (void)thumb_max;

        DrawAssetTile(draw_list, cell_min, cell_max, thumb_min, image_size, item_width,
                      hovered, active, selected, entry.path(), is_directory, filename);

        if (clicked && is_directory)
            content_browser_current_path = entry.path();   // listed again at the next frame
        else if (clicked)
            cba::state.selected = entry.path();

        if (!is_directory &&
            ImGui::BeginDragDropSource(
                ImGuiDragDropFlags_SourceAllowNullID))
        {
            const std::string asset_path =
                entry.path().generic_string();

            ImGui::SetDragDropPayload(
                "LYNX_ASSET_FILE",
                asset_path.c_str(),
                asset_path.size() + 1
            );

            ImGui::TextUnformatted(filename.c_str());
            ImGui::EndDragDropSource();
        }

        // Right click : open, rename, duplicate, copy / cut, delete...
        cba::ItemContextMenu(entry);

        ImGui::PopID();
    }

    if (entries.empty())
        ImGui::TextDisabled("Empty folder  (right click : new folder / new file)");
    else if (shown.empty())
        ImGui::TextDisabled("Nothing matches \"%s\"", content_browser_search);

    // Right click on the empty area, keyboard shortcuts, popups.
    cba::BackgroundContextMenu();
    cba::Shortcuts();
    cba::Popups();

    ImGui::EndChild();

    ImGui::SetWindowFontScale(0.8f);
    ImGui::End();
}


static void HandleLevelAssetDrop(lynx::Level* level)
{
    if (!level || isPlaying)
        return;

    ImGuiIO& io = ImGui::GetIO();

    // The HRL level is rendered outside ImGui. When no ImGui window is
    // capturing the mouse, a Content Browser payload can be dropped here.
    if (!SceneAcceptsMouse(io.WantCaptureMouse) ||
        !ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        return;
    }

    const ImGuiPayload* payload =
        ImGui::GetDragDropPayload();

    if (!payload ||
        !payload->Data ||
        payload->DataSize <= 0 ||
        !payload->DataType ||
        std::strcmp(
            payload->DataType,
            "LYNX_ASSET_FILE"
        ) != 0)
    {
        return;
    }

    std::string path(
        static_cast<const char*>(payload->Data),
        static_cast<size_t>(payload->DataSize)
    );

    if (!path.empty() && path.back() == '\0')
        path.pop_back();

    const std::filesystem::path asset_path(path);

    if (!IsPathInside(
            asset_path,
            content_browser_root))
    {
        return;
    }

    if (IsSupportedImageExtension(asset_path))
    {
        CreateActorFromAsset(
            asset_path,
            "StaticSprite",
            "texture_path"
        );
    }
    else if (IsSupportedAudioExtension(asset_path))
    {
        CreateActorFromAsset(
            asset_path,
            "AudioSource2D",
            "path"
        );
    }
}


// -----------------------------------------------------------------------------
// Asset path field (Details panel, string properties)
// -----------------------------------------------------------------------------
// Text field + "..." button. The button opens a small explorer rooted at
// assets/, and the value written back is ALWAYS relative to assets/
// (e.g. "sounds/boom.wav"). A Content Browser file can also be dropped
// directly on the field or on the button.
// Returns true when `value` was modified.
static bool DrawAssetPathField(std::string& value)
{
    static std::filesystem::path picker_dir;
    static char picker_search[128] = {};

    bool changed = false;

    auto accept_drop = [&]()
    {
        if (!ImGui::BeginDragDropTarget())
            return;

        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("LYNX_ASSET_FILE"))
        {
            std::string dropped(
                static_cast<const char*>(payload->Data),
                static_cast<size_t>(payload->DataSize)
            );

            if (!dropped.empty() && dropped.back() == '\0')
                dropped.pop_back();

            std::string relative;

            if (GetAssetRelativePath(
                    std::filesystem::path(dropped),
                    relative))
            {
                value = relative;
                changed = true;
            }
        }

        ImGui::EndDragDropTarget();
    };

    const float spacing =
        ImGui::GetStyle().ItemInnerSpacing.x;

    const float button_width =
        ImGui::GetFrameHeight() * 1.6f;

    ImGui::SetNextItemWidth(
        std::max(
            1.f,
            ImGui::GetContentRegionAvail().x -
                button_width -
                spacing
        )
    );

    char buffer[1024];

    std::snprintf(
        buffer,
        sizeof(buffer),
        "%s",
        value.c_str()
    );

    if (ImGui::InputText(
            "##value",
            buffer,
            sizeof(buffer)
        ))
    {
        value = buffer;
        changed = true;
    }

    accept_drop();

    ImGui::SameLine(0.f, spacing);

    if (ImGui::Button(
            "...",
            ImVec2(button_width, 0.f)
        ))
    {
        picker_dir = content_browser_root;
        picker_search[0] = '\0';
        ImGui::OpenPopup("AssetPicker");
    }

    accept_drop();

    ImGui::SetNextWindowSize(
        ImVec2(640.f, 600.f),
        ImGuiCond_Appearing
    );

    if (ImGui::BeginPopup("AssetPicker"))
    {
        std::error_code error;

        // Header : current folder, always shown relative to assets/.
        std::string current_relative =
            picker_dir.lexically_relative(content_browser_root)
                .generic_string();

        if (current_relative == ".")
            current_relative.clear();

        ImGui::TextDisabled(
            "assets/%s",
            current_relative.c_str()
        );

        if (ImGui::Button("None"))
        {
            value.clear();
            changed = true;
            ImGui::CloseCurrentPopup();
        }

        if (picker_dir != content_browser_root)
        {
            ImGui::SameLine();

            if (ImGui::Button(".."))
            {
                const std::filesystem::path parent =
                    picker_dir.parent_path();

                if (IsPathInside(parent, content_browser_root))
                    picker_dir = parent;
            }
        }

        ImGui::SameLine();

        ImGui::InputTextWithHint(
            "##PickerSearch",
            "Search...",
            picker_search,
            sizeof(picker_search)
        );

        ImGui::Separator();

        std::vector<std::filesystem::directory_entry> entries;

        if (std::filesystem::is_directory(picker_dir, error))
        {
            for (const auto& entry :
                 std::filesystem::directory_iterator(
                     picker_dir,
                     std::filesystem::directory_options::skip_permission_denied,
                     error))
            {
                if (error)
                    break;

                entries.push_back(entry);
            }
        }

        std::sort(
            entries.begin(),
            entries.end(),
            [](const auto& a, const auto& b)
            {
                const bool a_dir = a.is_directory();
                const bool b_dir = b.is_directory();

                if (a_dir != b_dir)
                    return a_dir > b_dir;

                return a.path().filename().string() <
                       b.path().filename().string();
            }
        );

        auto to_lower = [](std::string text)
        {
            std::transform(
                text.begin(),
                text.end(),
                text.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                }
            );

            return text;
        };

        const std::string search = to_lower(picker_search);

        ImGui::BeginChild("##AssetPickerList", ImVec2(0.f, 0.f));

        bool any_entry = false;

        for (const auto& entry : entries)
        {
            const bool is_directory = entry.is_directory();

            const std::string filename =
                entry.path().filename().string();

            if (!search.empty() &&
                to_lower(filename).find(search) == std::string::npos)
            {
                continue;
            }

            any_entry = true;

            const std::string relative =
                entry.path().lexically_relative(content_browser_root)
                    .generic_string();

            ImGui::PushID(relative.c_str());

            if (is_directory)
            {
                const std::string label = "[+] " + filename;

                if (ImGui::Selectable(label.c_str(), false))
                {
                    picker_dir = entry.path();
                    picker_search[0] = '\0';
                }
            }
            else
            {
                if (ImGui::Selectable(
                        filename.c_str(),
                        relative == value
                    ))
                {
                    value = relative;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::PopID();
        }

        if (!any_entry)
            ImGui::TextDisabled("Empty");

        ImGui::EndChild();

        ImGui::EndPopup();
    }

    return changed;
}


static void WindowCloseCallback(GLFWwindow* window)
{
    show_close_confirmation = true;

    glfwSetWindowShouldClose(
        window,
        GLFW_FALSE
    );
}


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
        else
        {
            // Ignore unknown settings so newer versions remain backwards
            // compatible with older settings files.
            std::string ignored;
            file >> ignored;
        }
    }
}

static void ApplyRuntimeSettings(GLFWwindow* window)
{
    if (window)
    {
        glfwSwapInterval(
            appSettings.vsync ? 1 : 0
        );
    }

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
constexpr float kCameraMaxZ = 3000.f;
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


static void SavePlaySnapshot()
{
    play_snapshot_valid = false;

    ClearBrushPreview();

    if (!editor_level)
        return;

    const std::string voxel_path =
        (std::filesystem::path("assets") / kPlayBackupVoxelName).string();

    HRL_SaveVoxelWorldAllFile(scene, voxel_path.c_str());

    editor_level->SaveToFile(kPlayBackupLevelFile);

    play_snapshot_valid = true;

    std::cout << "[PLAY] Snapshot saved\n";
}


// Editor <-> Game. Used by the F3 shortcut and by the toolbar Play/Stop button.
static void TogglePlayMode()
{
    if (!editor_engine)
        return;


    if (!isPlaying)
    {
        // EDITOR -> GAME
        // Remember the current state so Stop can put everything back.
        SavePlaySnapshot();

        // Clear editor selection and hide the gizmo in game mode.
        editing_object = HRL_INVALID_ID;
        editing_actor = nullptr;

        HRL_SetGizmoVisible(
            gizmo,
            HRL_FALSE
        );

        SetPlaying(editor_engine, editor_viewport, editor_gameplay_cam, true);

        std::cout << "[PLAY] Switched to GAME mode\n";
    }
    else
    {
        // GAME -> EDITOR
        SetPlaying(editor_engine, editor_viewport, editor_camera, false);

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
static void DrawToolbar()
{
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();

    const float height =
        ImGui::GetFrameHeight() +
        ImGui::GetStyle().WindowPadding.y * 2.f;

    if (ImGui::BeginViewportSideBar(
            "##Toolbar",
            main_viewport,
            ImGuiDir_Up,
            height,
            ImGuiWindowFlags_NoSavedSettings))
    {
        auto tooltip = [](const char* text)
        {
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", text);
        };

        // -----------------------------------------------------------------
        // Project : which project is open, switch to another one
        // -----------------------------------------------------------------
        {
            const std::string project_label =
                (currentProject.name.empty() ? std::string("Project") : currentProject.name) +
                "##ProjectButton";

            if (lynx::editor::icons::ButtonWithLabel(project_label.c_str(), lynx::editor::icons::Icon::Project))
                ImGui::OpenPopup("ProjectPopup");

            tooltip("Project");

            if (ImGui::BeginPopup("ProjectPopup"))
            {
                std::error_code relative_error;
                const std::filesystem::path module_relative =
                    std::filesystem::relative(
                        currentProject.module_path,
                        currentProject.root,
                        relative_error
                    );

                ImGui::TextDisabled("Project");
                ImGui::Separator();
                ImGui::TextUnformatted(currentProject.name.c_str());
                ImGui::TextDisabled("%s", currentProject.root.string().c_str());
                ImGui::TextDisabled(
                    "Game DLL : %s",
                    (relative_error ? currentProject.module_path : module_relative)
                        .generic_string().c_str()
                );
                ImGui::Separator();

                if (ImGui::MenuItem("Open another project...", nullptr, false, !isPlaying))
                {
                    switch_project_requested = true;
                    show_close_confirmation = true;
                }

                ImGui::EndPopup();
            }

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
        }

        // Editing tools are unavailable while the game is running.
        ImGui::BeginDisabled(isPlaying);

        if (lynx::editor::icons::Button("Save", lynx::editor::icons::Icon::Save))
            SaveEditor();
        tooltip("Save the level (Ctrl+S)");

        ImGui::SameLine();

        if (lynx::editor::icons::Button("Undo", lynx::editor::icons::Icon::Undo) && !brushPainting)
            UndoLastEditorAction();
        tooltip("Undo (Ctrl+Z)");

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        auto mode_button = [&](const char* label, lynx::editor::icons::Icon icon, int index, const char* tip)
        {
            const bool active = (gizmoModeIndex == index);

            if (lynx::editor::icons::Button(label, icon, lynx::editor::icons::kThemeTint, active))
                SetGizmoModeIndex(index);

            tooltip(tip);
        };

        mode_button("Translate", lynx::editor::icons::Icon::Translate, 0, "Translate (Space cycles the gizmo modes)");
        ImGui::SameLine();
        mode_button("Rotate", lynx::editor::icons::Icon::Rotate, 1, "Rotate (Space cycles the gizmo modes)");
        ImGui::SameLine();
        mode_button("Scale", lynx::editor::icons::Icon::Scale, 2, "Scale (Space cycles the gizmo modes)");

        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Play / Stop stays available in both modes (green triangle / red square).
        if (lynx::editor::icons::ButtonWithLabel(
                isPlaying ? "Stop##PlayStop" : "Play##PlayStop",
                isPlaying ? lynx::editor::icons::Icon::Stop : lynx::editor::icons::Icon::Play,
                isPlaying ? ImVec4(0.82f, 0.22f, 0.20f, 1.f) : ImVec4(0.22f, 0.62f, 0.26f, 1.f)))
            TogglePlayMode();

        tooltip(isPlaying ? "Stop the game, back to the editor (F3)" : "Play the game in the editor (F3)");

        // Reload the game DLL. Only a REQUEST is raised here : the actual reload
        // happens in main(), between two frames, never in the middle of the ImGui
        // frame (the panels are still holding actor pointers at this point).
        ImGui::SameLine();

        // Compile the game (Build.bat of the project) while the editor runs :
        // the editor uses a copy of the DLL, build/<game>.dll is free.
        ImGui::BeginDisabled(lynx::editor::game_build::IsRunning());

        if (lynx::editor::icons::ButtonWithLabel("Compile", lynx::editor::icons::Icon::Compile))
            lynx::editor::game_build::Start();

        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip(
                "Compile the game (Build.bat of the project)%s",
                appSettings.reloadAfterBuild
                    ? "\nthen reload it if the build succeeded."
                    : "."
            );
        }

        ImGui::SameLine();

        // Ship Game : compiled game + packed assets in a folder (ShipGame.h).
        ImGui::BeginDisabled(isPlaying || lynx::editor::ship_game::IsBusy());
        if (lynx::editor::icons::ButtonWithLabel("Ship Game", lynx::editor::icons::Icon::Project))
            lynx::editor::ship_game::Open();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Make the game playable without the editor :\n"
                              "compile it, pack assets/ inside the executable,\n"
                              "copy it with its DLLs to a folder.");

        ImGui::SameLine();

        if (lynx::editor::icons::ButtonWithLabel("Reload Game", lynx::editor::icons::Icon::Reload))
        {
            module_changed_notice = false;
            reload_game_requested = true;
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Reload %s\n"
                "Stops the game, saves pending level edits, destroys every actor,\n"
                "unloads the DLL, loads it again and rebuilds the level.",
                currentProject.module_path.filename().string().c_str()
            );
        }

        // Build state / DLL rebuilt outside the editor.
        {
            const std::string build_status = lynx::editor::game_build::ToolbarStatus();

            if (!build_status.empty())
            {
                ImGui::SameLine();

                const bool failed = build_status == "Build failed";

                if (failed)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1.f));

                ImGui::TextUnformatted(build_status.c_str());

                if (failed)
                    ImGui::PopStyleColor();

                if (ImGui::IsItemClicked())
                    lynx::editor::game_build::WindowOpen() = true;

                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Click : build output");
            }
            else if (hot_reload_pending)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(reload at Stop)");
            }
            else if (module_changed_notice)
            {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.80f, 0.35f, 1.f));
                ImGui::TextUnformatted("DLL rebuilt");
                ImGui::PopStyleColor();

                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("The game DLL was rebuilt : \"Reload Game\" to use it.");
            }
        }

        ImGui::SameLine();


        // -----------------------------------------------------------------
        // Settings
        // -----------------------------------------------------------------
        ImGui::SameLine();

        if (lynx::editor::icons::Button("Settings", lynx::editor::icons::Icon::Settings))
            ImGui::OpenPopup("SettingsPopup");

        if (ImGui::BeginPopup("SettingsPopup"))
        {
            ImGui::TextDisabled("Application settings");
            ImGui::Separator();

            if (ImGui::Checkbox(
                    "Reload the game after \"Compile\"",
                    &appSettings.reloadAfterBuild
                ))
            {
                SaveSettings();
            }

            if (ImGui::Checkbox(
                    "Reload the game when its DLL is rebuilt",
                    &appSettings.autoReloadOnChange
                ))
            {
                SaveSettings();
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Build.bat or CLion rebuilt build/<game>.dll while the editor runs.\n"
                    "While playing, the reload waits for Stop."
                );
            }

            ImGui::Separator();

            if (ImGui::Checkbox(
                    "VSync",
                    &appSettings.vsync
                ))
            {
                ApplyRuntimeSettings(nullptr);
                glfwSwapInterval(
                    appSettings.vsync ? 1 : 0
                );
                SaveSettings();
            }

            ImGui::Separator();
            ImGui::TextDisabled("Audio");

            if (ImGui::SliderFloat(
                    "Master volume",
                    &appSettings.masterVolume,
                    0.f,
                    2.f,
                    "%.2f"
                ))
            {
                lynx::SetMasterVolume(
                    appSettings.masterVolume
                );
                SaveSettings();
            }

            ImGui::Separator();
            ImGui::TextDisabled("Editor");

            if (ImGui::SliderFloat(
                    "Camera speed",
                    &appSettings.cameraSpeed,
                    1.f,
                    400.f,
                    "%.0f"
                ))
            {
                cameraSpeed = appSettings.cameraSpeed;
                SaveSettings();
            }

            ImGui::Separator();
            ImGui::TextDisabled("Voxels");

            int voxelMode =
                static_cast<int>(appSettings.voxelRenderMode);

            const char* voxelModes[] =
            {
                "Flat",
                "Smooth",
                "Blocky"
            };

            if (ImGui::Combo(
                    "Render mode",
                    &voxelMode,
                    voxelModes,
                    IM_ARRAYSIZE(voxelModes)
                ))
            {
                appSettings.voxelRenderMode =
                    static_cast<HRL_EVoxelRenderMode>(voxelMode);

                HRL_SetVoxelRenderMode(
                    scene,
                    appSettings.voxelRenderMode
                );

                SaveSettings();
            }

            ImGui::TextDisabled("Units : 1 = 1 voxel (positions, sizes, speeds)");

            ImGui::Separator();

            if (ImGui::Button("Reset to defaults"))
            {
                appSettings = AppSettings{};
                cameraSpeed = appSettings.cameraSpeed;

                ApplyRuntimeSettings(
                    ImGui::GetCurrentContext()
                        ? glfwGetCurrentContext()
                        : nullptr
                );

                SaveSettings();
            }

            ImGui::EndPopup();
        }

        tooltip("Application settings");

        ImGui::SameLine();

        // Use a regular button + popup instead of BeginMenu().
        // BeginMenu() can open its submenu when merely hovering a toolbar item,
        // which makes the rest of the toolbar difficult to use.
        if (lynx::editor::icons::ButtonWithLabel("Windows", lynx::editor::icons::Icon::Windows))
            ImGui::OpenPopup("WindowsPopup");

        if (ImGui::BeginPopup("WindowsPopup"))
        {
            ImGui::TextDisabled("Editor windows");
            ImGui::Separator();
            EditorWindowCheckbox("Viewport", &editorWindows.viewport, lynx::editor::icons::Icon::Camera);
            EditorWindowCheckbox("Outliner", &editorWindows.outliner, lynx::editor::icons::Icon::Outliner);
            EditorWindowCheckbox("Place Actors", &editorWindows.placeActors, lynx::editor::icons::Icon::PlaceActors);
            EditorWindowCheckbox("Content Browser", &editorWindows.contentBrowser, lynx::editor::icons::Icon::ContentBrowser);
            EditorWindowCheckbox("Details", &editorWindows.details, lynx::editor::icons::Icon::Details);
            EditorWindowCheckbox("Color Picking", &editorWindows.colorPicking, lynx::editor::icons::Icon::ColorPick);
            EditorWindowCheckbox("Paint", &editorWindows.config, lynx::editor::icons::Icon::Paint);
            EditorWindowCheckbox("Camera Shake", &editorWindows.cameraShake, lynx::editor::icons::Icon::Camera);
            EditorWindowCheckbox("Input Settings", &editorWindows.inputSettings, lynx::editor::icons::Icon::Input);
            EditorWindowCheckbox("Commands (Python / AI)", &editorWindows.commands, lynx::editor::icons::Icon::Commands);
            EditorWindowCheckbox("Git", &editorWindows.git, lynx::editor::icons::Icon::Git);
            EditorWindowCheckbox("Console", &editorWindows.console, lynx::editor::icons::Icon::Console);
            EditorWindowCheckbox("Profiler", &editorWindows.profiler, lynx::editor::icons::Icon::Profiler);
            ImGui::Separator();
            ImGui::TextDisabled("All editor windows are hidden during Play.");
            ImGui::EndPopup();
        }
        tooltip("Show or hide editor windows");

        // Lynxie : opens Commands on her tab.
        ImGui::SameLine();
        {
            // Same height as the text buttons of the toolbar.
            const float icon_height =
                ImGui::GetFrameHeight() - ImGui::GetStyle().FramePadding.y * 2.f;

            if (lynx::editor::lynxie_icon::Button("Lynxie", icon_height))
            {
                editorWindows.commands = true;
                SaveEditorWindowVisibility();
                lynx::editor::commands_window::ShowLynxie();
            }
        }
        tooltip("Lynxie : ask a question about the engine, or what to change in the level");

        // Master volume stays available in both modes.
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(
            ImGui::GetFontSize() * 8.f
        );

        if (ImGui::SliderFloat(
                "##MasterVolume",
                &appSettings.masterVolume,
                0.f,
                2.f,
                "Volume %.2f"
            ))
        {
            lynx::SetMasterVolume(
                appSettings.masterVolume
            );

            SaveSettings();
        }

        tooltip("Master volume");

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(
            ImGui::GetFontSize() * 8.f
        );

        if (ImGui::SliderFloat(
                "##CameraSpeed",
                &cameraSpeed,
                1.f,
                400.f,
                "Camera %.0f"
            ))
        {
            appSettings.cameraSpeed = cameraSpeed;
            SaveSettings();
        }

        tooltip("Camera speed");

        if (editor_dirty && !isPlaying)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("* unsaved changes");
        }
    }

    ImGui::End();
}


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
        if (!it->is_regular_file(e) || it->path().extension() != ".xml")
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
    file << std::setprecision(9) << camX << " " << camY << " " << camZ << "\n";
}


// Collision debug overlay (F4). The flag lives inside the game DLL, so it has to
// be pushed again every time the DLL is loaded (see editor::EndGameReload).
static bool show_collision_debug = false;


static void ApplyCollisionDebugToGame()
{
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

        HRL_SetCameraPerspectiveFov(
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


        HRL_SetCameraLocation(
            editor_camera,
            camX,
            camY,
            camZ
        );


        HRL_SetCameraRotation(
            editor_camera,
            0.f,
            -90.f,
            0.f
        );



        return editor_camera;
    }


    void Init(
        GLFWwindow* win,
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

        // Must be registered before ImGui installs (and chains) its own callbacks.
        glfwSetWindowCloseCallback(
            win,
            WindowCloseCallback
        );

        glfwSetDropCallback(
            win,
            DropCallback
        );


        // Custom title bar (Windows) : before ImGui, which chains its own window procedure.
        lynx::editor::title_bar::Install(win);

        InitImGui(win);
        LoadEditorWindowVisibility();

        // Widget Editor : textures / fonts / nested widgets with the asset picker.
        lynx::editor::widget_editor::SetAssetFieldDrawer([](std::string& value)
        {
            return DrawAssetPathField(value);
        });
        lynx::editor::graph_editors::SetAssetFieldDrawer([](std::string& value)
        {
            return DrawAssetPathField(value);
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


    }


    void Shutdown()
    {
        SaveEditorCamera();
        SaveEditorWindowVisibility();

        ShutdownImGui();
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
        }

        // 2. The level is about to be rebuilt from world.xml : write the pending
        //    edits first (this needs the actors to still be alive).
        if (editor_dirty)
            SaveEditor();

        // 3. Forget every actor pointer.
        editing_actor = nullptr;
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
    bool BlockGameInput()
    {
        return ImGui::GetIO().KeyShift;
    }


    bool WantsMouse()
    {
        return !SceneAcceptsMouse(ImGui::GetIO().WantCaptureMouse);
    }


    void OnLeftMousePress(GLFWwindow* window)
    {
        if (isPlaying)
            return;

        double mouseX;
        double mouseY;

        glfwGetCursorPos(
            window,
            &mouseX,
            &mouseY
        );

        MouseToScene(mouseX, mouseY, mouseX, mouseY);

        const ImGuiIO& io = ImGui::GetIO();

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
        }
    }


    void OnScroll(double yoffset)
    {
        if (!SceneAcceptsMouse(ImGui::GetIO().WantCaptureMouse))
            return;

        const ImGuiIO& io = ImGui::GetIO();

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

        // Normal wheel = camera Z.
        camZ =
            ClampCameraZ(
                camZ -
                static_cast<float>(yoffset) * std::max(2.f, camZ * 0.08f)
            );
    }


    // Everything editor related that runs BEFORE the engine update.
    void Tick(
        GLFWwindow* win,
        float dt)
    {
        HRL_id camera = editor_camera;

        ImGuiIO& io =
            ImGui::GetIO();

        double mouseX;
        double mouseY;

        glfwGetCursorPos(
            win,
            &mouseX,
            &mouseY
        );

        // C = pick the voxel under the mouse. HRL expects framebuffer
        // coordinates, while GLFW returns window coordinates. Do this
        // conversion independently from the editor viewport conversion.
        static bool c_was_down = false;
        const bool c_down =
            glfwGetKey(win, GLFW_KEY_C) == GLFW_PRESS;

        if (c_down &&
            !c_was_down &&
            !isPlaying &&
            !io.WantTextInput &&
            !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
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
            glfwGetKey(
                win,
                GLFW_KEY_F4
            ) == GLFW_PRESS;


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
            glfwGetKey(
                win,
                GLFW_KEY_LEFT_CONTROL
            ) == GLFW_PRESS ||
            glfwGetKey(
                win,
                GLFW_KEY_RIGHT_CONTROL
            ) == GLFW_PRESS;


        const bool s_down =
            glfwGetKey(
                win,
                GLFW_KEY_S
            ) == GLFW_PRESS;


        const bool ctrl_s =
            ctrl_down &&
            s_down;


        const bool javascript_editor_handles_shortcuts =
            lynx::editor::script_editors::HasKeyboardFocus() ||
            lynx::editor::widget_editor::HasFocus() ||
            lynx::editor::graph_editors::HasFocus();


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

        if (!isPlaying &&
            !io.KeyCtrl &&
            !io.WantTextInput)
        {
            // Speed depends on the camera height : slow when close, fast when far.
            const float moveSpeed = GetCameraSpeedForHeight();

            if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS)
                camY += moveSpeed * dt;

            if (!ctrl_down &&
                glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS)
                camY -= moveSpeed * dt;

            if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS)
                camX += moveSpeed * dt;

            if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS)
                camX -= moveSpeed * dt;

            camZ = ClampCameraZ(camZ);

            HRL_SetCameraLocation(
                camera,
                camX,
                camY,
                camZ
            );
        }


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
                }

                dragging_object = true;

                if (pos_changed)
                {
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
            glfwGetMouseButton(
                win,
                GLFW_MOUSE_BUTTON_LEFT
            ) != GLFW_PRESS)
        {
            const std::string actor_id =
                gizmoUndoActorId;

            const lynx::transform previous_transform =
                gizmoUndoTransform;

            PushEditorUndo([actor_id, previous_transform]()
            {
                lynx::Actor* actor =
                    FindEditorActor(actor_id);

                if (!actor)
                    return;

                actor->transform = previous_transform;
                SetActorSelected(actor);
                MarkEditorDirty();
            });

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
                    glfwGetMouseButton(
                        win,
                        GLFW_MOUSE_BUTTON_LEFT
                    ) == GLFW_PRESS;


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

                if (glfwGetKey(
                        win,
                        GLFW_KEY_F1
                    ) == GLFW_PRESS)
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

                if (glfwGetKey(
                        win,
                        GLFW_KEY_F2
                    ) == GLFW_PRESS)
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
            glfwGetKey(
                win,
                GLFW_KEY_TAB
            ) == GLFW_PRESS;

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
            glfwGetKey(
                win,
                GLFW_KEY_SPACE
            ) == GLFW_PRESS;

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
            glfwGetKey(
                win,
                GLFW_KEY_F3
            ) == GLFW_PRESS;

        if (f3_down && !f3_was_down)
            TogglePlayMode();

        f3_was_down =
            f3_down;
    }


    // ImGui frame : all panels, shortcuts, popups. Ends with the ImGui render.
    // "Viewport" window : the scene texture, stretched over the whole window.
    static void DrawViewportWindow()
    {
        sceneViewport.visible = false;
        sceneViewport.hovered = false;

        // First launch : cover the whole work area.
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(
            main_viewport->WorkPos,
            ImGuiCond_FirstUseEver
        );

        ImGui::SetNextWindowSize(
            main_viewport->WorkSize,
            ImGuiCond_FirstUseEver
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(0.f, 0.f)
        );

        const bool open =
            ImGui::Begin(
                "Viewport",
                nullptr,
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse
            );

        ImGui::PopStyleVar();

        if (open)
        {
            const ImVec2 size = ImGui::GetContentRegionAvail();

            const unsigned int texture_id =
                HRL_GL_GetSceneTextureGL_ID(scene);

            if (texture_id != 0 && size.x > 1.f && size.y > 1.f)
            {
                const ImVec2 pos = ImGui::GetCursorScreenPos();

                // OpenGL textures are bottom-up : flip V.
                ImGui::Image(
                    (ImTextureID)(intptr_t)texture_id,
                    size,
                    ImVec2(0.f, 1.f),
                    ImVec2(1.f, 0.f)
                );

                // Invisible button on top : it receives the clicks (so the
                // window is not dragged) and keeps the mouse captured for
                // the scene while a brush stroke goes outside the window.
                ImGui::SetCursorScreenPos(pos);

                ImGui::InvisibleButton(
                    "##scene_viewport",
                    size,
                    ImGuiButtonFlags_MouseButtonLeft |
                    ImGuiButtonFlags_MouseButtonRight |
                    ImGuiButtonFlags_MouseButtonMiddle
                );

                sceneViewport.visible = true;
                sceneViewport.x = pos.x;
                sceneViewport.y = pos.y;
                sceneViewport.w = size.x;
                sceneViewport.h = size.y;
                // A Place Actors drag keeps its source item active, which can
                // make IsItemHovered() report false over this image even when
                // the cursor is visibly inside it. Test the viewport bounds
                // directly so releasing a class here commits its instance.
                const ImVec2 viewportMax(
                    pos.x + size.x,
                    pos.y + size.y
                );
                sceneViewport.hovered = ImGui::IsMouseHoveringRect(
                    pos,
                    viewportMax,
                    false
                );

                // ImGui sizes are logical pixels; HRL renders into a physical
                // framebuffer. Resize the scene to exactly match the image.
                int windowW = 0, windowH = 0;
                int framebufferW = 0, framebufferH = 0;
                glfwGetWindowSize(glfwGetCurrentContext(), &windowW, &windowH);
                glfwGetFramebufferSize(glfwGetCurrentContext(), &framebufferW, &framebufferH);
                const float scaleX = windowW > 0 ? static_cast<float>(framebufferW) / windowW : 1.f;
                const float scaleY = windowH > 0 ? static_cast<float>(framebufferH) / windowH : 1.f;
                const int renderW = std::max(1, static_cast<int>(std::lround(size.x * scaleX)));
                const int renderH = std::max(1, static_cast<int>(std::lround(size.y * scaleY)));
                sceneViewport.x -= main_viewport->Pos.x;
                sceneViewport.y -= main_viewport->Pos.y;
                if (!sceneViewport.hasRenderSize ||
                    renderW != sceneViewport.renderW || renderH != sceneViewport.renderH)
                {
                    sceneViewport.renderW = renderW;
                    sceneViewport.renderH = renderH;
                    sceneViewport.hasRenderSize = true;
                    // Renderer + widgets of the game (DPI scale).
                    editor_engine->SetRenderSize(renderW, renderH);
                }

                // Right click on an actor (without moving) : its menu (Convert to voxels...).
                static lynx::Actor* menu_actor = nullptr;
                const ImGuiIO& vio = ImGui::GetIO();
                if (!isPlaying && sceneViewport.hovered &&
                    ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
                    vio.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < 25.f)
                {
                    double scene_x = 0.0, scene_y = 0.0;
                    MouseToScene(vio.MousePos.x, vio.MousePos.y, scene_x, scene_y);
                    const HRL_id object = HRL_GL_GetHoveredObject(scene, (int)scene_x, (int)scene_y, nullptr);
                    lynx::Actor* actor = object != HRL_INVALID_ID
                        ? static_cast<lynx::Actor*>(HRL_GetMeshUserHandle(object))
                        : nullptr;
                    if (actor && editor_level)
                    {
                        SetActorSelected(actor);
                        menu_actor = actor;
                        ImGui::OpenPopup("##ViewportActorMenu");
                    }
                }
                if (ImGui::BeginPopup("##ViewportActorMenu"))
                {
                    const auto& actors = editor_level ? editor_level->GetActors() : std::vector<lynx::Actor*>{};
                    if (menu_actor && std::find(actors.begin(), actors.end(), menu_actor) != actors.end())
                    {
                        const std::string title = menu_actor->GetTypeName() +
                            (menu_actor->object_id_.empty() ? std::string() : " [" + menu_actor->object_id_ + "]");
                        ImGui::TextDisabled("%s", title.c_str());
                        ImGui::Separator();
                        lynx::editor::sprite_voxels::MenuItem(menu_actor);
                    }
                    else
                    {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }
        }

        ImGui::End();
    }


    void DrawUI(GLFWwindow* win)
    {
        lynx::Level* level = editor_level;

        BeginImGuiFrame();

        if (editorWindows.viewport)
            DrawViewportWindow();

        // Resolve the release after the viewport has updated its hover state
        // for this frame. This also lets SceneAcceptsMouse distinguish the
        // scene image from the surrounding ImGui panels.
        if (!isPlaying)
            HandleActorPlacementDrag(level);

        // Title bar of the window (Windows : replaces the native one), above the toolbar.
        // Engine version after Lynxie ; the tab shows the project infos when hovered.
        {
            static std::string shown_module = "\x01";
            if (shown_module != currentProject.module_path.string())
            {
                shown_module = currentProject.module_path.string();
                lynx::editor::project_info::Project info;
                info.name = currentProject.name;
                info.root = currentProject.root;
                info.assets_dir = currentProject.assets_dir;
                info.build_dir = currentProject.build_dir;
                info.module_path = currentProject.module_path;
                info.engine_version = lynx::GetEngineVersion();
                lynx::editor::project_info::SetProject(info);
                lynx::editor::title_bar::SetVersionText("Lynx " + info.engine_version);
                lynx::editor::title_bar::SetTitleTooltip(lynx::editor::project_info::DrawTooltip);
            }
        }
        lynx::editor::title_bar::Draw(
            "Lynx - " + (currentProject.name.empty() ? std::string("Project") : currentProject.name),
            [] { show_close_confirmation = true; },
            []
            {
                editorWindows.commands = true;
                SaveEditorWindowVisibility();
                lynx::editor::commands_window::ShowLynxie();
            });

        DrawToolbar();

        // The Console stays visible while playing : output and errors of the game.
        // (In the editor, it is drawn with the other windows below.)
        if (isPlaying && editorWindows.console)
            lynx::editor::output_console::Draw(&editorWindows.console);

        // The Profiler too (profiling the game is the point) ; called even when
        // closed : it stops recording then.
        if (isPlaying)
            lynx::editor::profiler_window::Draw(&editorWindows.profiler);

        // While playing, the toolbar and the Console are the only ImGui windows that remain visible.
        if (!isPlaying)
        {
            ImGuiIO& io =
            ImGui::GetIO();




        // --------------------------------------------------------
        // Undo
        // --------------------------------------------------------

        if (!isPlaying &&
            !lynx::editor::input_settings::BlocksEditorShortcuts() &&
            !lynx::editor::widget_editor::HasFocus() &&
            !lynx::editor::graph_editors::HasFocus() &&
            io.KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_Z, false) &&
            !io.WantTextInput &&
            !brushPainting)
        {
            UndoLastEditorAction();
        }


        // --------------------------------------------------------
        // Delete selected actor
        // --------------------------------------------------------

        if (!isPlaying &&
            editing_actor &&
            !io.WantTextInput &&
            !content_browser_focused &&
            !lynx::editor::widget_editor::HasFocus() &&
            !lynx::editor::graph_editors::HasFocus() &&
            !lynx::editor::input_settings::BlocksEditorShortcuts() &&
            ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            lynx::Actor* actor_to_delete = editing_actor;
            const EditorActorSnapshot deleted_snapshot =
                CaptureActorSnapshot(actor_to_delete);

            PushEditorUndo([deleted_snapshot]()
            {
                if (!editor_level)
                    return;

                if (!deleted_snapshot.object_id.empty() &&
                    FindEditorActor(deleted_snapshot.object_id))
                {
                    return;
                }

                lynx::Actor* restored =
                    RestoreActorSnapshot(
                        editor_level,
                        deleted_snapshot
                    );

                if (restored)
                    SetActorSelected(restored);
            });

            editing_actor = nullptr;
            editing_object = HRL_INVALID_ID;

            HRL_SetGizmoVisible(
                gizmo,
                HRL_FALSE
            );

            level->DestroyActor(actor_to_delete);
            MarkEditorDirty();
        }


        // --------------------------------------------------------
        // Outliner
        // --------------------------------------------------------

        if (!isPlaying && editorWindows.outliner)
        {
            ImGui::Begin("Outliner");

            const auto& actors = level->GetActors();

            // One row of the tree : icon, name, drag, right click menu.
            auto draw_actor = [&](lynx::Actor* actor, size_t i)
            {

                    std::string typeName =
                        actor->GetTypeName();

                    std::string label = typeName;

                    if (!actor->object_id_.empty())
                    {
                        label += " [" + actor->object_id_ + "]";
                    }

                    label += "##OutlinerActor";
                    label += std::to_string(i);

                    const bool selected =
                        editing_actor == actor;

                    // Small icon of the class.
                    DrawActorTypePreview(typeName, ImGui::GetTextLineHeight());
                    ImGui::SameLine(0.f, ImGui::GetStyle().ItemSpacing.x * 0.5f);

                    if (ImGui::Selectable(
                            label.c_str(),
                            selected
                        ))
                    {
                        SetActorSelected(actor);
                    }

                    // Drag onto a folder (or the empty space : root).
                    if (ImGui::BeginDragDropSource())
                    {
                        lynx::Actor* dragged = actor;
                        ImGui::SetDragDropPayload("LYNX_OUTLINER_ACTOR", &dragged, sizeof(dragged));
                        ImGui::TextUnformatted(label.substr(0, label.find("##")).c_str());
                        ImGui::EndDragDropSource();
                    }


                    // Right-click context menu for this actor.
                    if (ImGui::BeginPopupContextItem())
                    {
                        if (ImGui::MenuItem("Duplicate"))
                        {
                            lynx::Actor* duplicated_actor =
                                level->SpawnActor(
                                    typeName.c_str()
                                );

                            if (duplicated_actor)
                            {
                                // Copy the transform and every registered property
                                // from the source actor to the new instance.
                                duplicated_actor->transform =
                                    actor->transform;

                                const auto& source_properties =
                                    actor->GetProperties();

                                auto& destination_properties =
                                    duplicated_actor->GetProperties();

                                for (const auto& [name, source_property] :
                                     source_properties)
                                {
                                    if (name == "object_id_")
                                        continue;

                                    auto destination_it =
                                        destination_properties.find(name);

                                    if (destination_it ==
                                        destination_properties.end())
                                    {
                                        continue;
                                    }

                                    const auto& destination_property =
                                        destination_it->second;

                                    if (source_property.GetType() !=
                                        destination_property.GetType())
                                    {
                                        continue;
                                    }

                                    switch (source_property.GetType())
                                    {
                                        case 0: // int
                                        {
                                            auto source_ptr =
                                                std::get_if<int*>(
                                                    &source_property.property_member
                                                );

                                            auto destination_ptr =
                                                std::get_if<int*>(
                                                    &destination_property.property_member
                                                );

                                            if (source_ptr && destination_ptr &&
                                                *source_ptr && *destination_ptr)
                                            {
                                                **destination_ptr = **source_ptr;
                                            }
                                            break;
                                        }

                                        case 1: // float
                                        {
                                            auto source_ptr =
                                                std::get_if<float*>(
                                                    &source_property.property_member
                                                );

                                            auto destination_ptr =
                                                std::get_if<float*>(
                                                    &destination_property.property_member
                                                );

                                            if (source_ptr && destination_ptr &&
                                                *source_ptr && *destination_ptr)
                                            {
                                                **destination_ptr = **source_ptr;
                                            }
                                            break;
                                        }

                                        case 2: // bool
                                        {
                                            auto source_ptr =
                                                std::get_if<bool*>(
                                                    &source_property.property_member
                                                );

                                            auto destination_ptr =
                                                std::get_if<bool*>(
                                                    &destination_property.property_member
                                                );

                                            if (source_ptr && destination_ptr &&
                                                *source_ptr && *destination_ptr)
                                            {
                                                **destination_ptr = **source_ptr;
                                            }
                                            break;
                                        }

                                        case 3: // string
                                        {
                                            auto source_ptr =
                                                std::get_if<std::string*>(
                                                    &source_property.property_member
                                                );

                                            auto destination_ptr =
                                                std::get_if<std::string*>(
                                                    &destination_property.property_member
                                                );

                                            if (source_ptr && destination_ptr &&
                                                *source_ptr && *destination_ptr)
                                            {
                                                **destination_ptr = **source_ptr;
                                            }
                                            break;
                                        }

                                        case 4: // vec2
                                        {
                                            auto source_ptr =
                                                std::get_if<lynx::vec2*>(
                                                    &source_property.property_member
                                                );

                                            auto destination_ptr =
                                                std::get_if<lynx::vec2*>(
                                                    &destination_property.property_member
                                                );

                                            if (source_ptr && destination_ptr &&
                                                *source_ptr && *destination_ptr)
                                            {
                                                **destination_ptr = **source_ptr;
                                            }
                                            break;
                                        }

                                        case 5: // vec3
                                        {
                                            auto source_ptr =
                                                std::get_if<lynx::vec3*>(
                                                    &source_property.property_member
                                                );

                                            auto destination_ptr =
                                                std::get_if<lynx::vec3*>(
                                                    &destination_property.property_member
                                                );

                                            if (source_ptr && destination_ptr &&
                                                *source_ptr && *destination_ptr)
                                            {
                                                **destination_ptr = **source_ptr;
                                            }
                                            break;
                                        }

                                        case 6: // vec4
                                        {
                                            auto source_ptr =
                                                std::get_if<lynx::vec4*>(
                                                    &source_property.property_member
                                                );

                                            auto destination_ptr =
                                                std::get_if<lynx::vec4*>(
                                                    &destination_property.property_member
                                                );

                                            if (source_ptr && destination_ptr &&
                                                *source_ptr && *destination_ptr)
                                            {
                                                **destination_ptr = **source_ptr;
                                            }
                                            break;
                                        }

                                        case 7: // transform
                                        {
                                            auto source_ptr =
                                                std::get_if<lynx::transform*>(
                                                    &source_property.property_member
                                                );

                                            auto destination_ptr =
                                                std::get_if<lynx::transform*>(
                                                    &destination_property.property_member
                                                );

                                            if (source_ptr && destination_ptr &&
                                                *source_ptr && *destination_ptr)
                                            {
                                                **destination_ptr = **source_ptr;
                                            }
                                            break;
                                        }

                                        default:
                                            break;
                                    }
                                }

                                // Generate a unique object ID for the copy.
                                std::string base_id =
                                    actor->object_id_.empty()
                                        ? typeName
                                        : actor->object_id_;

                                std::string duplicated_id =
                                    base_id + "_Copy";

                                int copy_index = 2;

                                while (level->GetActorFromID(
                                           duplicated_id.c_str()
                                       ))
                                {
                                    duplicated_id =
                                        base_id +
                                        "_Copy" +
                                        std::to_string(copy_index++);
                                }

                                duplicated_actor->object_id_ =
                                    duplicated_id;

                                const std::string duplicated_actor_id =
                                    duplicated_actor->object_id_;

                                PushEditorUndo([duplicated_actor_id]()
                                {
                                    if (lynx::Actor* duplicated =
                                            FindEditorActor(duplicated_actor_id))
                                    {
                                        if (editing_actor == duplicated)
                                        {
                                            editing_actor = nullptr;
                                            editing_object = HRL_INVALID_ID;
                                            HRL_SetGizmoVisible(gizmo, HRL_FALSE);
                                        }

                                        editor_level->DestroyActor(duplicated);
                                    }
                                });

                                SetActorSelected(duplicated_actor);
                                MarkEditorDirty();
                            }

                            ImGui::CloseCurrentPopup();
                        }


                        // Outliner folder of the actor.
                        OutlinerMoveToMenu(level, actor);

                        // Sprite -> voxels (dialog with the options).
                        lynx::editor::sprite_voxels::MenuItem(actor);
                        ImGui::Separator();

                        if (ImGui::MenuItem("Delete"))
                        {
                            const EditorActorSnapshot deleted_snapshot =
                                CaptureActorSnapshot(actor);

                            PushEditorUndo([deleted_snapshot]()
                            {
                                if (!editor_level ||
                                    (!deleted_snapshot.object_id.empty() &&
                                     FindEditorActor(deleted_snapshot.object_id)))
                                {
                                    return;
                                }

                                lynx::Actor* restored =
                                    RestoreActorSnapshot(
                                        editor_level,
                                        deleted_snapshot
                                    );

                                if (restored)
                                    SetActorSelected(restored);
                            });

                            if (editing_actor == actor)
                            {
                                editing_actor = nullptr;
                                editing_object = HRL_INVALID_ID;

                                HRL_SetGizmoVisible(
                                    gizmo,
                                    HRL_FALSE
                                );
                            }

                            level->DestroyActor(actor);
                            MarkEditorDirty();

                            ImGui::CloseCurrentPopup();
                        }

                        ImGui::EndPopup();
                    }
            };

            DrawOutlinerTree(level, draw_actor);

            ImGui::End();


            // --------------------------------------------------------
            // Place Actors
            // --------------------------------------------------------

            if (editorWindows.placeActors)
                PlaceActorsWindow(level);


            // --------------------------------------------------------
            // Content Browser
            // --------------------------------------------------------

            if (editorWindows.contentBrowser)
                DrawContentBrowser(level);


            // --------------------------------------------------------
            // Script editors (one window per open file)
            // --------------------------------------------------------

            lynx::editor::script_editors::DrawAll(central_dock_id);

            // Widget editors (one window per open .widget).
            lynx::editor::widget_editor::DrawAll(central_dock_id);

            // Anim Graph / Behavior Tree editors (one window per open file).
            lynx::editor::graph_editors::DrawAll(central_dock_id);


            // --------------------------------------------------------
            if (editorWindows.details)
            {
// Details
            // --------------------------------------------------------

            static bool details_undo_active = false;
            static EditorActorSnapshot details_undo_before;
            static std::string details_undo_actor_id;

            if (!details_undo_active && editing_actor)
            {
                details_undo_before =
                    CaptureActorSnapshot(editing_actor);
                details_undo_actor_id =
                    editing_actor->object_id_;
            }

            bool details_changed = false;

            ImGui::Begin("Details");

            if (!editing_actor)
            {
                ImGui::TextDisabled("No actor selected");
            }
            else
            {
                std::string typeName =
                    editing_actor->GetTypeName();

                ImGui::Text(
                    "%s",
                    typeName.c_str()
                );

                ImGui::Separator();


                // ----------------------------------------------------
                // Object ID
                // ----------------------------------------------------

                ImGui::Text("Object ID");

                char objectIdBuffer[512];

                std::snprintf(
                    objectIdBuffer,
                    sizeof(objectIdBuffer),
                    "%s",
                    editing_actor->object_id_.c_str()
                );

                if (ImGui::InputText(
                        "##ObjectID",
                        objectIdBuffer,
                        sizeof(objectIdBuffer)
                    ))
                {
                    editing_actor->object_id_ =
                        objectIdBuffer;
                    details_changed = true;
                    MarkEditorDirty();
                }


                ImGui::Separator();


                // ----------------------------------------------------
                // Properties
                // ----------------------------------------------------

                const auto& properties =
                    editing_actor->GetProperties();

                // Transform properties first, in their own section
                // (Location / Rotation / Scale rows, like Unreal).
                for (const auto& [name, prop] : properties)
                {
                    if (prop.GetType() != 7)
                        continue;

                    auto ptr = std::get_if<lynx::transform*>(&prop.property_member);
                    if (!ptr || !*ptr)
                        continue;

                    ImGui::PushID(name.c_str());
                    const std::string header =
                        (name == "transform" ? std::string("Transform") : name) + "##section";

                    if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen) &&
                        lynx::editor::property_widgets::BeginTable("##transform"))
                    {
                        lynx::transform& value = **ptr;

                        if (lynx::editor::property_widgets::TransformRows(value))
                        {
                            // Keep the editor gizmo synchronized with
                            // the transform edited from the Details panel.
                            SyncGizmoFromTransform(value);
                            details_changed = true;
                            MarkEditorDirty();
                        }

                        lynx::editor::property_widgets::EndTable();
                    }
                    ImGui::PopID();
                }

                if (!ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    // section closed
                }
                else if (properties.size() <= 1)
                {
                    ImGui::TextDisabled(
                        "No properties"
                    );
                }
                else if (lynx::editor::property_widgets::BeginTable("##properties"))
                {
                    for (const auto& [name, prop] : properties)
                    {
                        // object_id_ is edited separately above, transforms in their section.
                        if (name == "object_id_" || name == "outliner_folder" || prop.GetType() == 7)
                            continue;

                        ImGui::PushID(name.c_str());

                            // Vectors draw their own row (label with its menu) ;
                            // a vec3 / vec4 called "...color..." is a color picker.
                            const int type = prop.GetType();
                            const bool is_color = (type == 5 || type == 6) && IsColorPropertyName(name);
                            if (type < 4 || type > 6 || is_color)
                                lynx::editor::property_widgets::RowLabel(name.c_str());

                            bool changed = false;

                            if (is_color)
                            {
                                ImGui::SetNextItemWidth(-FLT_MIN);
                                if (auto ptr = std::get_if<lynx::vec3*>(&prop.property_member); ptr && *ptr)
                                {
                                    float rgb[3] = { (*ptr)->x, (*ptr)->y, (*ptr)->z };
                                    if (ImGui::ColorEdit3("##color", rgb, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                                    {
                                        **ptr = lynx::vec3(rgb[0], rgb[1], rgb[2]);
                                        changed = true;
                                    }
                                }
                                else if (auto ptr4 = std::get_if<lynx::vec4*>(&prop.property_member); ptr4 && *ptr4)
                                {
                                    float rgba[4] = { (*ptr4)->x, (*ptr4)->y, (*ptr4)->z, (*ptr4)->w };
                                    if (ImGui::ColorEdit4("##color", rgba, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                                    {
                                        (*ptr4)->x = rgba[0];
                                        (*ptr4)->y = rgba[1];
                                        (*ptr4)->z = rgba[2];
                                        (*ptr4)->w = rgba[3];
                                        changed = true;
                                    }
                                }
                            }
                            else switch (prop.GetType())
                            {
                                case 0: // int
                                {
                                    if (auto ptr =
                                            std::get_if<int*>(&prop.property_member))
                                    {
                                        if (*ptr && name == "auto_possess_player")
                                            changed = DrawAutoPossessCombo(**ptr);
                                        else if (*ptr)
                                            changed = ImGui::DragInt(
                                                "##value",
                                                *ptr,
                                                1.0f
                                            );
                                    }
                                    break;
                                }

                                case 1: // float
                                {
                                    if (auto ptr =
                                            std::get_if<float*>(&prop.property_member))
                                    {
                                        if (*ptr)
                                            changed = ImGui::DragFloat(
                                                "##value",
                                                *ptr,
                                                0.05f
                                            );
                                    }
                                    break;
                                }

                                case 2: // bool
                                {
                                    if (auto ptr =
                                            std::get_if<bool*>(&prop.property_member))
                                    {
                                        if (*ptr)
                                            changed = ImGui::Checkbox(
                                                "##value",
                                                *ptr
                                            );
                                    }
                                    break;
                                }

                                case 3: // string (text field + asset explorer)
                                {
                                    if (auto ptr =
                                            std::get_if<std::string*>(&prop.property_member))
                                    {
                                        if (*ptr)
                                            changed = DrawAssetPathField(**ptr);
                                    }
                                    break;
                                }

                                case 4: // vec2
                                {
                                    if (auto ptr =
                                            std::get_if<lynx::vec2*>(&prop.property_member))
                                    {
                                        if (*ptr)
                                        {
                                            float values[2] =
                                            {
                                                (*ptr)->x,
                                                (*ptr)->y
                                            };

                                            if (lynx::editor::property_widgets::VectorRow(
                                                    name.c_str(),
                                                    values,
                                                    2,
                                                    0.05f,
                                                    nullptr
                                                ))
                                            {
                                                (*ptr)->x = values[0];
                                                (*ptr)->y = values[1];
                                                changed = true;
                                            }
                                        }
                                    }
                                    break;
                                }

                                case 5: // vec3
                                {
                                    if (auto ptr =
                                            std::get_if<lynx::vec3*>(&prop.property_member))
                                    {
                                        if (*ptr)
                                        {
                                            float values[3] =
                                            {
                                                (*ptr)->x,
                                                (*ptr)->y,
                                                (*ptr)->z
                                            };

                                            if (lynx::editor::property_widgets::VectorRow(
                                                    name.c_str(),
                                                    values,
                                                    3,
                                                    0.05f,
                                                    nullptr
                                                ))
                                            {
                                                (*ptr)->x = values[0];
                                                (*ptr)->y = values[1];
                                                (*ptr)->z = values[2];
                                                changed = true;
                                            }
                                        }
                                    }
                                    break;
                                }

                                case 6: // vec4
                                {
                                    if (auto ptr =
                                            std::get_if<lynx::vec4*>(&prop.property_member))
                                    {
                                        if (*ptr)
                                        {
                                            float values[4] =
                                            {
                                                (*ptr)->x,
                                                (*ptr)->y,
                                                (*ptr)->z,
                                                (*ptr)->w
                                            };

                                            if (lynx::editor::property_widgets::VectorRow(
                                                    name.c_str(),
                                                    values,
                                                    4,
                                                    0.05f,
                                                    nullptr
                                                ))
                                            {
                                                (*ptr)->x = values[0];
                                                (*ptr)->y = values[1];
                                                (*ptr)->z = values[2];
                                                (*ptr)->w = values[3];
                                                changed = true;
                                            }
                                        }
                                    }
                                    break;
                                }

                                case 7: // transform
                                {
                                    if (auto ptr =
                                            std::get_if<lynx::transform*>(&prop.property_member))
                                    {
                                        if (*ptr)
                                        {
                                            lynx::transform& value = **ptr;

                                            float location[3] =
                                            {
                                                value.location.x,
                                                value.location.y,
                                                value.location.z
                                            };

                                            float rotation[3] =
                                            {
                                                value.rotation.x,
                                                value.rotation.y,
                                                value.rotation.z
                                            };

                                            float scale[3] =
                                            {
                                                value.scale.x,
                                                value.scale.y,
                                                value.scale.z
                                            };

                                            ImGui::Text("Location");
                                            changed |= ImGui::DragFloat3(
                                                "##location",
                                                location,
                                                0.05f
                                            );

                                            ImGui::Text("Rotation");
                                            changed |= ImGui::DragFloat3(
                                                "##rotation",
                                                rotation,
                                                0.5f
                                            );

                                            ImGui::Text("Scale");
                                            changed |= ImGui::DragFloat3(
                                                "##scale",
                                                scale,
                                                0.05f
                                            );

                                            value.location.x = location[0];
                                            value.location.y = location[1];
                                            value.location.z = location[2];

                                            value.rotation.x = rotation[0];
                                            value.rotation.y = rotation[1];
                                            value.rotation.z = rotation[2];

                                            value.scale.x = scale[0];
                                            value.scale.y = scale[1];
                                            value.scale.z = scale[2];

                                            // Keep the editor gizmo synchronized with
                                            // the transform edited from the Details panel.
                                            if (changed && editing_actor)
                                            {
                                                SyncGizmoFromTransform(value);
                                            }
                                        }
                                    }
                                    break;
                                }

                                default:
                                {
                                    ImGui::TextDisabled(
                                        "%s",
                                        lynx::PropertyToString(prop).c_str()
                                    );
                                    break;
                                }
                            }

                            if (changed)
                            {
                                details_changed = true;
                                MarkEditorDirty();
                            }

                        ImGui::PopID();
                    }

                    lynx::editor::property_widgets::EndTable();
                }
            }


            ImGui::End();

            if (details_changed)
                details_undo_active = true;

            if (details_undo_active &&
                !ImGui::IsAnyItemActive())
            {
                const std::string actor_id_after =
                    editing_actor
                        ? editing_actor->object_id_
                        : details_undo_actor_id;

                const EditorActorSnapshot undo_snapshot =
                    details_undo_before;

                PushEditorUndo([undo_snapshot, actor_id_after]()
                {
                    lynx::Actor* actor =
                        FindEditorActor(actor_id_after);

                    if (!actor)
                        return;

                    actor->object_id_ =
                        undo_snapshot.object_id;
                    actor->transform =
                        undo_snapshot.transform;

                    auto& properties = actor->GetProperties();

                    for (const auto& [name, value] :
                         undo_snapshot.properties)
                    {
                        auto it = properties.find(name);

                        if (it != properties.end())
                            ApplyEditorProperty(it->second, value);
                    }

                    SetActorSelected(actor);
                    MarkEditorDirty();
                });

                details_undo_active = false;
                details_undo_actor_id.clear();
            }
        }


        // --------------------------------------------------------
                    }

        // --------------------------------------------------------
        // Color Picking
        // --------------------------------------------------------

        if (editorWindows.colorPicking)
        {

        // Color and properties of the voxel type of the brush.
        if (ImGui::Begin("Color Picking", &editorWindows.colorPicking))
            lynx::editor::voxel_types::DrawEditor(brushVoxelType, scene);
        ImGui::End();


        // --------------------------------------------------------
        }

        // --------------------------------------------------------
        // Config
        // --------------------------------------------------------

        if (editorWindows.config)
        {

        ImGui::Begin("Paint");


        // --------------------------------------------------------
        // Brush Mode
        // --------------------------------------------------------

        ImGui::Text(
            "Brush Mode"
        );

        ImGui::TextDisabled(
            "Tab = switch mode"
        );


        if (ImGui::Button("Paint"))
        {
            brushPaintEnabled = true;
        }


        if (brushPaintEnabled)
        {
            ImVec2 min =
                ImGui::GetItemRectMin();


            ImVec2 max =
                ImGui::GetItemRectMax();


            ImGui::GetWindowDrawList()->AddRect(
                ImVec2(
                    min.x - 2.f,
                    min.y - 2.f
                ),
                ImVec2(
                    max.x + 2.f,
                    max.y + 2.f
                ),
                IM_COL32(
                    255,
                    255,
                    255,
                    255
                ),
                2.f,
                0,
                2.f
            );
        }


        ImGui::SameLine();


        if (ImGui::Button(
                "No Painting"
            ))
        {
            brushPaintEnabled = false;
        }


        if (!brushPaintEnabled)
        {
            ImVec2 min =
                ImGui::GetItemRectMin();


            ImVec2 max =
                ImGui::GetItemRectMax();


            ImGui::GetWindowDrawList()->AddRect(
                ImVec2(
                    min.x - 2.f,
                    min.y - 2.f
                ),
                ImVec2(
                    max.x + 2.f,
                    max.y + 2.f
                ),
                IM_COL32(
                    255,
                    255,
                    255,
                    255
                ),
                2.f,
                0,
                2.f
            );
        }


        if (brushPaintEnabled)
        {
            ImGui::SliderInt(
                "Brush Radius",
                &brushRadius,
                1,
                50
            );

            // ----------------------------------------------------
            // Brush shapes
            // ----------------------------------------------------

            ImGui::Text("Brush");

            struct BrushShapeButton
            {
                BrushShape shape;
                const char* name;
            };

            static const BrushShapeButton brushShapeButtons[] =
            {
                { BrushShape::Single,      "Single" },
                { BrushShape::Circle,      "Circle" },
                { BrushShape::Square,      "Square" },
                { BrushShape::Diamond,     "Diamond" },
                { BrushShape::Horizontal,  "Horizontal" },
                { BrushShape::Vertical,    "Vertical" },
                { BrushShape::Cross,            "Cross" },
                { BrushShape::Line,             "Line" },
                { BrushShape::Rectangle,        "Rectangle" },
                { BrushShape::RectangleOutline, "Rectangle Outline" },
                { BrushShape::CircleOutline,    "Circle Outline" }
            };

            auto drawBrushShapeIcon = [](
                ImDrawList* drawList,
                const ImVec2& min,
                const ImVec2& max,
                BrushShape shape
            )
            {
                const ImVec2 center(
                    (min.x + max.x) * 0.5f,
                    (min.y + max.y) * 0.5f
                );

                const float iconSize =
                    std::min(max.x - min.x, max.y - min.y) * 0.62f;

                const float cell = iconSize / 9.f;
                const int iconRadius = 4;
                const ImU32 iconColor = IM_COL32(
                    225, 225, 225, 255
                );

                for (int y = -iconRadius; y <= iconRadius; ++y)
                {
                    for (int x = -iconRadius; x <= iconRadius; ++x)
                    {
                        bool filled = false;

                        switch (shape)
                        {
                            case BrushShape::Single:
                                filled = x == 0 && y == 0;
                                break;

                            case BrushShape::Circle:
                                filled = x * x + y * y <= 10;
                                break;

                            case BrushShape::Square:
                                filled = true;
                                break;

                            case BrushShape::Diamond:
                                filled = std::abs(x) + std::abs(y) <= 4;
                                break;

                            case BrushShape::Horizontal:
                                filled = y == 0;
                                break;

                            case BrushShape::Vertical:
                                filled = x == 0;
                                break;

                            case BrushShape::Cross:
                                filled = x == 0 || y == 0;
                                break;

                            case BrushShape::Line:
                                filled = y == 0;
                                break;

                            case BrushShape::Rectangle:
                                filled = std::abs(x) <= 3 && std::abs(y) <= 3;
                                break;

                            case BrushShape::RectangleOutline:
                                filled = (std::abs(x) == 3 || std::abs(y) == 3) &&
                                         std::abs(x) <= 3 && std::abs(y) <= 3;
                                break;

                            case BrushShape::CircleOutline:
                                {
                                    const int d = x * x + y * y;
                                    filled = d >= 8 && d <= 18;
                                }
                                break;
                        }

                        if (!filled)
                            continue;

                        const ImVec2 cellMin(
                            center.x + (x - 0.5f) * cell,
                            center.y + (y - 0.5f) * cell
                        );

                        const ImVec2 cellMax(
                            cellMin.x + cell,
                            cellMin.y + cell
                        );

                        drawList->AddRectFilled(
                            cellMin,
                            cellMax,
                            iconColor,
                            1.f
                        );
                    }
                }
            };

            for (int i = 0;
                 i < static_cast<int>(IM_ARRAYSIZE(brushShapeButtons));
                 ++i)
            {
                const BrushShapeButton& brushButton =
                    brushShapeButtons[i];

                ImGui::PushID(
                    static_cast<int>(brushButton.shape)
                );

                const ImVec2 buttonSize(52.f, 52.f);

                if (i > 0)
                    ImGui::SameLine();

                const ImVec2 cursor = ImGui::GetCursorScreenPos();

                const bool pressed =
                    ImGui::InvisibleButton(
                        "##BrushShape",
                        buttonSize
                    );

                const bool hovered =
                    ImGui::IsItemHovered();

                ImDrawList* drawList =
                    ImGui::GetWindowDrawList();

                const ImU32 background =
                    hovered
                        ? IM_COL32(70, 70, 70, 255)
                        : IM_COL32(45, 45, 45, 255);

                drawList->AddRectFilled(
                    cursor,
                    ImVec2(
                        cursor.x + buttonSize.x,
                        cursor.y + buttonSize.y
                    ),
                    background,
                    4.f
                );

                drawBrushShapeIcon(
                    drawList,
                    cursor,
                    ImVec2(
                        cursor.x + buttonSize.x,
                        cursor.y + buttonSize.y
                    ),
                    brushButton.shape
                );

                const bool selected =
                    brushShape == brushButton.shape;

                drawList->AddRect(
                    ImVec2(
                        cursor.x + 1.f,
                        cursor.y + 1.f
                    ),
                    ImVec2(
                        cursor.x + buttonSize.x - 1.f,
                        cursor.y + buttonSize.y - 1.f
                    ),
                    selected
                        ? IM_COL32(255, 255, 255, 255)
                        : IM_COL32(90, 90, 90, 255),
                    4.f,
                    0,
                    selected ? 2.f : 1.f
                );

                if (pressed)
                    brushShape = brushButton.shape;

                if (hovered)
                    ImGui::SetTooltip("%s", brushButton.name);

                ImGui::PopID();
            }

            // Radius has no effect on the single-voxel brush.
            if (brushShape == BrushShape::Single)
            {
                ImGui::TextDisabled("Single voxel");
            }


            ImGui::Checkbox(
                "Paint empty voxels",
                &brushPaintEmptyVoxels
            );


            ImGui::SliderFloat(
                "Brush Density",
                &brushDensity,
                0.f,
                1.f,
                "%.2f"
            );


            ImGui::Text(
                "Voxel Type"
            );


            // ----------------------------------------------------
            // Voxel types : swatches, "+" (new type), right click menu
            // ----------------------------------------------------

            bool editVoxelType = false;
            lynx::editor::voxel_types::DrawPalette(brushVoxelType, scene, editVoxelType);
            if (editVoxelType)
            {
                editorWindows.colorPicking = true;
                ImGui::SetWindowFocus("Color Picking");
            }
        }


            ImGui::End();
        }

        // --------------------------------------------------------
        // Camera Shake
        // --------------------------------------------------------

        if (editorWindows.cameraShake)
        {

        ImGui::Begin(
            "Camera Shake"
        );


        ImGui::Separator();


        ImGui::Text(
            "Camera Shake"
        );


        ImGui::Checkbox(
            "Enable Camera Shake",
            &lynx::GetCameraShake().enabled
        );


        if (lynx::GetCameraShake().enabled)
        {
            ImGui::SliderFloat(
                "Shake Duration",
                &lynx::GetCameraShake().duration,
                0.01f,
                2.0f,
                "%.2f s"
            );


            ImGui::SliderFloat(
                "Position Amplitude",
                &lynx::GetCameraShake().positionAmplitude,
                0.f,
                20.f,
                "%.2f"
            );


            ImGui::SliderFloat(
                "Rotation Amplitude",
                &lynx::GetCameraShake().rotationAmplitude,
                0.f,
                20.f,
                "%.2f deg"
            );


            ImGui::SliderFloat(
                "Shake Frequency",
                &lynx::GetCameraShake().frequency,
                1.f,
                60.f,
                "%.1f Hz"
            );


            ImGui::SliderFloat(
                "Shake Falloff",
                &lynx::GetCameraShake().falloff,
                0.1f,
                4.0f,
                "%.2f"
            );


            if (ImGui::Button(
                    "Test Camera Shake"
                ))
            {
                lynx::GetCameraShake().Trigger();
            }
        }


        ImGui::End();


        // --------------------------------------------------------
        }

        // --------------------------------------------------------
        // Input Settings (project input.json)
        // --------------------------------------------------------

        lynx::editor::game_build::DrawWindow();

        {
            const bool was_open = editorWindows.inputSettings;

            lynx::editor::input_settings::Draw(&editorWindows.inputSettings);

            if (was_open != editorWindows.inputSettings)
                SaveEditorWindowVisibility();
        }

        // Python scripts / editor console / AI (MCP) connection.
        {
            const bool was_open = editorWindows.commands;

            lynx::editor::commands_window::Draw(&editorWindows.commands);

            if (was_open != editorWindows.commands)
                SaveEditorWindowVisibility();
        }

        // Ship Game window (and its steps : build, packing).
        lynx::editor::ship_game::SetProject(currentProject.root, currentProject.name);
        lynx::editor::ship_game::Draw([]() { SaveEditor(); });

        // Profiler (frame times, LYNX_PROFILE_SCOPE zones, Tracy).
        {
            const bool was_open = editorWindows.profiler;

            lynx::editor::profiler_window::Draw(&editorWindows.profiler);

            if (was_open != editorWindows.profiler)
                SaveEditorWindowVisibility();
        }

        // Output of the editor (stdout / stderr, the Windows console is disabled).
        {
            const bool was_open = editorWindows.console;

            lynx::editor::output_console::Draw(&editorWindows.console);

            if (was_open != editorWindows.console)
                SaveEditorWindowVisibility();
        }

        // Git (version control of the project folder).
        {
            const bool was_open = editorWindows.git;

            git_window::Draw(&editorWindows.git, editor_dirty, []() { SaveEditor(); });

            if (was_open != editorWindows.git)
                SaveEditorWindowVisibility();
        }

// Level drag & drop
        // --------------------------------------------------------

        HandleLevelAssetDrop(level);


        // Sprite -> voxels dialog (right click on a sprite actor).
        lynx::editor::sprite_voxels::Draw(SpriteVoxelHost());

        // --------------------------------------------------------
        // Close confirmation
        // --------------------------------------------------------

        if (show_close_confirmation)
        {
            ImGui::OpenPopup("Save before quitting?");
            show_close_confirmation = false;
        }

        if (ImGui::BeginPopupModal(
                "Save before quitting?",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize
            ))
        {
            if (switch_project_requested)
            {
                ImGui::TextUnformatted(
                    "The project browser opens after this editor closes."
                );
                ImGui::Separator();
            }

            const bool scripts_dirty =
                lynx::editor::script_editors::HasUnsavedChanges() ||
                lynx::editor::widget_editor::HasUnsavedChanges() ||
                lynx::editor::graph_editors::HasUnsavedChanges();

            if (editor_dirty || scripts_dirty)
            {
                if (editor_dirty)
                    ImGui::TextUnformatted(
                        "The level has unsaved changes."
                    );
                if (scripts_dirty)
                    ImGui::TextUnformatted(
                        "Some scripts or widgets have unsaved changes (marked * in their tab)."
                    );
                ImGui::TextUnformatted(
                    "Do you want to save before quitting?"
                );
                ImGui::Separator();

                if (ImGui::Button("Save and Quit"))
                {
                    if (editor_dirty)
                        SaveEditor();
                    lynx::editor::script_editors::SaveAll();
                    lynx::editor::widget_editor::SaveAll();
                    lynx::editor::graph_editors::SaveAll();
                    glfwSetWindowShouldClose(win, GLFW_TRUE);
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine();

                if (ImGui::Button("Quit without Saving"))
                {
                    glfwSetWindowShouldClose(win, GLFW_TRUE);
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine();

                if (ImGui::Button("Cancel"))
                {
                    switch_project_requested = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            else
            {
                ImGui::TextUnformatted(
                    "There are no unsaved level changes."
                );
                ImGui::Separator();

                if (ImGui::Button("Quit"))
                {
                    glfwSetWindowShouldClose(win, GLFW_TRUE);
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine();

                if (ImGui::Button("Cancel"))
                {
                    switch_project_requested = false;
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }


        if (editor_warning_pending)
        {
            ImGui::OpenPopup("Warning##EditorWarning");
            editor_warning_pending = false;
        }

        if (ImGui::BeginPopupModal(
                "Warning##EditorWarning",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize
            ))
        {
            ImGui::TextUnformatted(editor_warning.c_str());
            ImGui::Separator();

            if (ImGui::Button("OK"))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }

        if (show_copy_error)
        {
            ImGui::OpenPopup("Content Browser Error");
            show_copy_error = false;
        }

        if (ImGui::BeginPopupModal(
                "Content Browser Error",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize
            ))
        {
            ImGui::TextWrapped(
                "%s",
                copy_error_message.c_str()
            );

            if (ImGui::Button("OK"))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }

        } // !isPlaying: hide every editor window while the game is running

        EndImGuiFrame();
    }
}



static void FramebufferSizeCallback(
    GLFWwindow*,
    int width,
    int height)
{
    // In the editor the rendered scene belongs to the ImGui viewport. Its
    // dimensions are sent to HRL from DrawViewportWindow(); resizing the host
    // window must not change the scene's render dimensions.
    if (editorWindows.viewport)
        return;
    if (lynx::Engine* engine = lynx::Engine::Get())
        engine->SetRenderSize(width, height);
}


static double lastMouseX = 0.0;
static double lastMouseY = 0.0;
static bool haveLastMousePosition = false;


static void MouseMove(
    GLFWwindow*,
    double x,
    double y)
{
    double scene_x = x;
    double scene_y = y;

    MouseToScene(x, y, scene_x, scene_y);

    HRL_MouseMovedCallback(
        static_cast<float>(scene_x),
        static_cast<float>(scene_y)
    );

    if (haveLastMousePosition)
    {
        lynx::InjectMouseMove(
            static_cast<float>(x - lastMouseX),
            static_cast<float>(y - lastMouseY)
        );
    }

    lastMouseX = x;
    lastMouseY = y;
    haveLastMousePosition = true;
}


static void MouseButtonCallback(
    GLFWwindow* window,
    int button,
    int action,
    int mods)
{
    // An ImGui window must completely own the click.
    // Keep the decision made on PRESS until RELEASE so a click that starts
    // on an ImGui window can never leak a release into the game.
    static bool gameMouseButtonDown[GLFW_MOUSE_BUTTON_LAST + 1] = {};

    // Input Settings : "press the key to bind".
    if (lynx::editor::input_settings::OnMouseButton(button, action))
        return;

    const bool editorOwnsMouse = editor::WantsMouse();
    const bool blocked = editor::BlockGameInput() || editorOwnsMouse;

    if (button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST)
    {
        if (action == GLFW_PRESS)
        {
            gameMouseButtonDown[button] = !blocked;

            if (!blocked)
                lynx::InjectMouseButtonDown(button);
        }
        else if (action == GLFW_RELEASE)
        {
            if (gameMouseButtonDown[button])
                lynx::InjectMouseButtonUp(button);

            gameMouseButtonDown[button] = false;
        }
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT &&
        action == GLFW_PRESS)
    {
        editor::OnLeftMousePress(window);
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        HRL_MouseButtonCallback(
            HRL_MOUSE_BUTTON_LEFT,
            action == GLFW_PRESS
                ? HRL_MOUSE_PRESS
                : HRL_MOUSE_RELEASE
        );
    }
}


void ScrollCallback(
    GLFWwindow* window,
    double xoffset,
    double yoffset)
{
    if (lynx::editor::input_settings::OnScroll(yoffset))
        return;

    if (!editor::BlockGameInput() &&
        !editor::WantsMouse())
    {
        lynx::InjectMouseWheel(
            static_cast<float>(yoffset)
        );
    }

    editor::OnScroll(yoffset);
}


void inject_keys_callback(
    GLFWwindow* window,
    int key,
    int scancode,
    int action,
    int mods)
{
    // Input Settings : "press the key to bind".
    if (lynx::editor::input_settings::OnKey(key, action))
        return;

    if (editor::BlockGameInput())
        return;

    if (action == GLFW_PRESS)
    {
        lynx::InjectKeyDown(key);
    }
    else if (action == GLFW_RELEASE)
    {
        lynx::InjectKeyUp(key);
    }
}


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


int main(int argc, char** argv)
{
    // No Windows console (-mwindows) : stdout / stderr go to the Console window.
    lynx::editor::output_console::Start();

#ifdef _WIN32
    SetUnhandledExceptionFilter(EditorCrashFilter);
#endif
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

    lynx::editor::game_build::Init(currentProject.root, currentProject.module_path);

    // Game DLL not rebuilt after an engine change : warn (crash otherwise).
    ShowEditorWarning(
        lynx::host::CheckModuleUpToDate(currentProject.module_path)
    );

    // Project files (working directory = project root).
    lynx::editor::splash::SetStep("Reading the project settings", 0.28f);

    LoadSettings();
    MigrateToVoxelUnits();

    LoadInputConfig();


    lynx::editor::splash::SetStep("Creating the editor window", 0.32f);

    glfwInit();

    // Hidden until the project is loaded : the splash is shown meanwhile, the
    // window appears (maximized) once its content is ready.
    glfwWindowHint(
        GLFW_VISIBLE,
        GLFW_FALSE
    );

    glfwWindowHint(
        GLFW_CONTEXT_VERSION_MAJOR,
        3
    );

    glfwWindowHint(
        GLFW_CONTEXT_VERSION_MINOR,
        3
    );

    glfwWindowHint(
        GLFW_OPENGL_PROFILE,
        GLFW_OPENGL_CORE_PROFILE
    );


    const std::string window_title =
        "Lynx - " + currentProject.name;

    GLFWwindow* win =
        glfwCreateWindow(
            1280,
            720,
            window_title.c_str(),
            nullptr,
            nullptr
        );

    if (!win)
    {
        std::cerr << "[WINDOW] Could not create the editor window\n";
        lynx::editor::splash::Close();
        return 1;
    }

    // Maximized when it is shown, at the end of the loading (a hidden window
    // would be shown by glfwMaximizeWindow on Windows).

    glfwMakeContextCurrent(win);

    glfwSwapInterval(
        appSettings.vsync ? 1 : 0
    );

    // icon.png of the project (assets) : this window and the next ones
    // (window_icon) ; otherwise the editor icon (exe resource / icon.png
    // next to the editor).
    lynx::editor::window_icon::SetImage(lynx::fs::ReadBinary("icon.png"));
    lynx::editor::window_icon::Apply(win);


    glfwSetFramebufferSizeCallback(
        win,
        FramebufferSizeCallback
    );

    glfwSetMouseButtonCallback(
        win,
        MouseButtonCallback
    );

    glfwSetCursorPosCallback(
        win,
        MouseMove
    );

    glfwSetScrollCallback(
        win,
        ScrollCallback
    );

    glfwSetKeyCallback(
        win,
        inject_keys_callback
    );



    lynx::editor::splash::SetStep("Starting the renderer", 0.38f);

    HRL_Init(HRL_OPENGL_33);

    int winX, winY;
    glfwGetWindowSize(win, &winX, &winY);

    HRL_InitContext(
        winX,
        winY,
        (void*)glfwGetProcAddress
    );

    // Size known by the widgets too (the Viewport window sets its own).
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
    // assets/save_file.txt holds the path (relative to assets/) of the voxel
    // world. A new project without it gets "world.vox".

    lynx::editor::splash::SetStep("Loading the voxel world", 0.48f);

    auto save_file =
        lynx::fs::ReadBinary(
            "save_file.txt"
        );

    std::string save_data(
        reinterpret_cast<const char*>(
            save_file.data()
        ),
        save_file.size()
    );


    while (!save_data.empty() &&
           (save_data.back() == '\n' ||
            save_data.back() == '\r' ||
            save_data.back() == ' ' ||
            save_data.back() == '\t'))
    {
        save_data.pop_back();
    }

    if (save_data.empty())
    {
        save_data = "world.vox";

        std::cout
            << "[WORLD] assets/save_file.txt missing or empty, using "
            << save_data
            << "\n";
    }


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


    lynx::editor::splash::SetStep("Loading the level", 0.72f);

    auto* level = engine->CreateLevel(
        "world.xml"
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
        win,
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


    HRL_id post_mat =
        HRL_CreateMaterial(
            HRL_DEFAULT_POST_PROCESS_SHADER
        );


    HRL_MaterialSetFloat(
        post_mat,
        "vignetteStrength",
        0.4f
    );

    // Same post process on the viewport of every player, also the ones the
    // game creates while playing (Engine::CreatePlayer).
    auto add_post_process = [post_mat](lynx::PlayerController* player)
    {
        if (player && player->GetViewportBackend() != HRL_INVALID_ID)
            HRL_CreatePostProcess(player->GetViewportBackend(), post_mat, 1);
    };

    for (lynx::PlayerController* player : engine->GetPlayers())
        add_post_process(player);

    engine->ED_player_created.Subscribe(add_post_process);


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

    glfwMaximizeWindow(win);
    glfwShowWindow(win);
    glfwFocusWindow(win);

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

        // 4. Rebuild the level (world.xml was saved in step 1 if it was dirty).
        level =
            engine->CreateLevel(
                "world.xml"
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
            lynx::host::CheckModuleUpToDate(currentProject.module_path)
        );

        std::cout << "[RELOAD] Done\n";

        return true;
    };


    // ------------------------------------------------------------
    // Main loop
    // ------------------------------------------------------------

    LYNX_PROFILE_THREAD("Main");

    while (!glfwWindowShouldClose(win))
    {
        {
            LYNX_PROFILE_SCOPE("Events");
            glfwPollEvents();
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
                glfwSetWindowShouldClose(
                    win,
                    GLFW_TRUE
                );

                continue;
            }

            // Do not count the reload time as a frame.
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

                glfwSetWindowShouldClose(
                    win,
                    GLFW_TRUE
                );

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
        // Clear
        // --------------------------------------------------------

        glClearColor(
            0.f,
            0.f,
            0.f,
            1.f
        );


        glClear(
            GL_COLOR_BUFFER_BIT |
            GL_DEPTH_BUFFER_BIT
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


            char title[256];


            std::snprintf(
                title,
                sizeof(title),
                "%s",
                window_title.c_str()
            );


            glfwSetWindowTitle(
                win,
                title
            );


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
                win,
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


        // --------------------------------------------------------
        // Gameplay camera (moved by the game : follow, shake...)
        // --------------------------------------------------------

        if (gameHooks.update_gameplay_camera)
        {
            LYNX_PROFILE_SCOPE("Game camera");
            gameHooks.update_gameplay_camera(gameplay_cam, dt);
        }


        // --------------------------------------------------------
        // Editor UI (ImGui)
        // --------------------------------------------------------

        {
            LYNX_PROFILE_SCOPE("Editor UI");
            editor::DrawUI(win);
        }

        // Screenshots / frame waits asked by commands (the frame is drawn).
        ProcessFrameEndCommands();


        {
            // Includes the wait for vsync when it is on.
            LYNX_PROFILE_SCOPE("Swap buffers");
            glfwSwapBuffers(win);
        }

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

    lynx::Engine::Destroy();

    editor::Shutdown();


    // "Project > Open another project..." : a new editor, on the browser.
    if (switch_project_requested)
        lynx::host::LaunchEditorProcess({});


    return 0;
}
