// =============================================================================
// LYNX_EDITOR
// -----------------------------------------------------------------------------
// Defined  -> editor build (ImGui panels, gizmo, brush, undo, content browser...)
// Not defined -> game only: nothing editor related is compiled, and the game
//               starts playing right away.
//
// Comment the define below out to build the game alone (or remove it and pass
// -DLYNX_EDITOR from CMake with target_compile_definitions for the editor target).
// =============================================================================
#ifndef LYNX_EDITOR
#define LYNX_EDITOR
#endif

#include <core/Engine.h>
#include <../src/Lynx.h>
#ifdef LYNX_EDITOR
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>   // BeginViewportSideBar (toolbar)
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#endif
#include <glfw/glfw3.h>
#include <hrl/hrl.h>
#include <hrl/hrl_gl.h>

#include <iostream>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <memory>
#include <variant>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#endif

#include "Common.h"
#include "GamepadInput.h"
#include "audio/AudioCommon.h"
#include "core/Private/SystemModule.h"
#include "gameplay/Private/InputManager.h"

bool isPlaying = false;

HRL_id scene;


// =============================================================================
// Custom mouse cursors (editor + game)
// -----------------------------------------------------------------------------
// Each image is read with lynx::fs::ReadBinary, loaded by HRL
// (HRL_CreateTexture), then its pixels are read back from OpenGL
// (HRL_GL_GetTextureGLID + glGetTexImage) to build a GLFW cursor.
// Must be called AFTER HRL_Init / HRL_InitContext.
//
// Every image is optional : a missing file falls back to the standard system
// cursor of the same kind (hand, text caret, resize). If NO image is found
// at all, the system cursors are left untouched.
// 32x32 is a good size (some systems refuse very large cursors).
// =============================================================================

enum CursorKind
{
    kCursorArrow = 0,
    kCursorSelect,      // hand / "you can click or select this"
    kCursorText,        // text selection (I-beam)
    kCursorLoading,     // busy (long blocking operations)
    kCursorResizeEW,    // resize left <-> right
    kCursorResizeNS,    // resize up <-> down
    kCursorResizeNWSE,  // resize diagonal, top-left <-> bottom-right
    kCursorResizeNESW,  // resize diagonal, top-right <-> bottom-left
    kCursorKindCount
};

#ifdef GLFW_RESIZE_NWSE_CURSOR
constexpr int kShapeNWSE = GLFW_RESIZE_NWSE_CURSOR;
constexpr int kShapeNESW = GLFW_RESIZE_NESW_CURSOR;
#else
constexpr int kShapeNWSE = 0;   // GLFW < 3.4 : no diagonal standard cursor
constexpr int kShapeNESW = 0;
#endif

struct CursorDef
{
    const char* file;
    int hotspotX;       // pixel that "clicks", -1 = center of the image
    int hotspotY;
    int fallbackShape;  // GLFW standard cursor used when the file is missing
};

// Tweak the hotspots here. (0,0) = top-left corner, like an arrow.
static const CursorDef kCursorDefs[kCursorKindCount] =
{
    /* Arrow      */ { "cur/cursor.png",             0,  0, 0                    },
    /* Select     */ { "cur/cursor_select.png",      0,  0, GLFW_HAND_CURSOR     },
    /* Text       */ { "cur/cursor_text.png",       -1, -1, GLFW_IBEAM_CURSOR    },
    /* Loading    */ { "cur/cursor_loading.png",    -1, -1, 0                    },
    /* ResizeEW   */ { "cur/cursor_resize_h.png",   -1, -1, GLFW_HRESIZE_CURSOR  },
    /* ResizeNS   */ { "cur/cursor_resize_v.png",   -1, -1, GLFW_VRESIZE_CURSOR  },
    /* ResizeNWSE */ { "cur/cursor_resize_nwse.png",-1, -1, kShapeNWSE           },
    /* ResizeNESW */ { "cur/cursor_resize_nesw.png",-1, -1, kShapeNESW           },
};

// Set to true if the cursors show up upside down.
constexpr bool kCursorFlipY = true;

// Size of the cursors : 1.0 = size of the png, 2.0 = twice bigger...
// For the best quality, prefer a bigger png (48x48, 64x64) and keep 1.0.
constexpr float kCursorScale = 1.3f;

// true  : sharp pixels (pixel art)       false : smooth (bilinear)
constexpr bool kCursorScaleNearest = true;

// Some systems refuse or clip bigger cursors.
constexpr int kCursorMaxSize = 128;

static GLFWcursor* cursors[kCursorKindCount] = {};
static GLFWwindow* cursorWindow = nullptr;
static bool customCursorsEnabled = false;
static int currentCursorKind = -1;
static int busyCursorDepth = 0;

static void SetCursorKind(int kind)
{
    if (!customCursorsEnabled || !cursorWindow)
        return;

    if (kind == currentCursorKind)
        return;

    currentCursorKind = kind;

    GLFWcursor* cursor =
        cursors[kind] ? cursors[kind] : cursors[kCursorArrow];

    glfwSetCursor(cursorWindow, cursor);
}

// Shows the "loading" cursor during a long blocking operation (save, play /
// stop, game reload...). The cursor is changed immediately, because the
// main loop does not run while the operation is in progress.
struct BusyCursorScope
{
    int previous;

    BusyCursorScope()
        : previous(currentCursorKind)
    {
        ++busyCursorDepth;
        SetCursorKind(kCursorLoading);
    }

    ~BusyCursorScope()
    {
        --busyCursorDepth;

        if (previous >= 0)
            SetCursorKind(previous);
    }
};

static std::vector<unsigned char> ScaleCursorPixels(
    const std::vector<unsigned char>& src,
    int w,
    int h,
    int new_w,
    int new_h)
{
    std::vector<unsigned char> dst(static_cast<size_t>(new_w) * new_h * 4);

    auto pixel = [&](int x, int y) -> const unsigned char*
    {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return &src[(static_cast<size_t>(y) * w + x) * 4];
    };

    for (int y = 0; y < new_h; ++y)
    {
        for (int x = 0; x < new_w; ++x)
        {
            unsigned char* out =
                &dst[(static_cast<size_t>(y) * new_w + x) * 4];

            const float fx = (x + 0.5f) * w / new_w - 0.5f;
            const float fy = (y + 0.5f) * h / new_h - 0.5f;

            if (kCursorScaleNearest)
            {
                const unsigned char* p =
                    pixel(static_cast<int>(std::floor(fx + 0.5f)),
                          static_cast<int>(std::floor(fy + 0.5f)));

                std::copy(p, p + 4, out);
                continue;
            }

            // Bilinear, weighted by alpha so transparent pixels do not
            // darken the edges of the cursor.
            const int x0 = static_cast<int>(std::floor(fx));
            const int y0 = static_cast<int>(std::floor(fy));
            const float tx = fx - x0;
            const float ty = fy - y0;

            float r = 0.f, g = 0.f, b = 0.f, a = 0.f;

            for (int j = 0; j < 2; ++j)
            {
                for (int i = 0; i < 2; ++i)
                {
                    const float weight =
                        (i ? tx : 1.f - tx) * (j ? ty : 1.f - ty);

                    const unsigned char* p = pixel(x0 + i, y0 + j);
                    const float wa = weight * p[3];

                    r += wa * p[0];
                    g += wa * p[1];
                    b += wa * p[2];
                    a += wa;
                }
            }

            if (a > 0.f)
            {
                out[0] = static_cast<unsigned char>(r / a + 0.5f);
                out[1] = static_cast<unsigned char>(g / a + 0.5f);
                out[2] = static_cast<unsigned char>(b / a + 0.5f);
            }
            else
            {
                out[0] = out[1] = out[2] = 0;
            }

            out[3] = static_cast<unsigned char>(std::min(a, 255.f) + 0.5f);
        }
    }

    return dst;
}

static GLFWcursor* CreateCursorFromImageFile(const CursorDef& def)
{
    const auto file =
        lynx::fs::ReadBinary(
            def.file
        );

    if (file.empty())
        return nullptr;

    // NOTE : HRL_CreateTexture takes the encoded image (png...) in memory.
    const HRL_id texture =
        HRL_CreateTexture(
            reinterpret_cast<const char*>(file.data()),
            file.size()
        );

    if (texture == HRL_INVALID_ID)
    {
        std::cerr << "[CURSOR] HRL_CreateTexture failed for "
                  << def.file << "\n";
        return nullptr;
    }

    const GLuint gl_id =
        static_cast<GLuint>(HRL_GL_GetTextureGL_ID(texture));

    if (gl_id == 0)
    {
        std::cerr << "[CURSOR] No OpenGL id for " << def.file << "\n";
        return nullptr;
    }

    // Read the pixels back, keeping HRL's GL state untouched.
    GLint previous_binding = 0;
    GLint previous_pack_alignment = 4;

    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_binding);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previous_pack_alignment);

    GLint w = 0;
    GLint h = 0;

    glBindTexture(GL_TEXTURE_2D, gl_id);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

    std::vector<unsigned char> pixels;

    if (w > 0 && h > 0)
    {
        pixels.resize(static_cast<size_t>(w) * h * 4);

        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTexImage(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            pixels.data()
        );
    }

    glPixelStorei(GL_PACK_ALIGNMENT, previous_pack_alignment);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_binding));

    if (pixels.empty())
    {
        std::cerr << "[CURSOR] " << def.file << " is empty\n";
        return nullptr;
    }

    if (kCursorFlipY)
    {
        const size_t row = static_cast<size_t>(w) * 4;

        for (int y = 0; y < h / 2; ++y)
        {
            std::swap_ranges(
                pixels.begin() + y * row,
                pixels.begin() + (y + 1) * row,
                pixels.begin() + (h - 1 - y) * row
            );
        }
    }

    // Enlarge the cursor (the OS shows a cursor at its pixel size).
    float applied_scale = 1.f;

    if (kCursorScale != 1.f)
    {
        applied_scale = kCursorScale;

        const float biggest = static_cast<float>(std::max(w, h));

        if (biggest * applied_scale > kCursorMaxSize)
            applied_scale = kCursorMaxSize / biggest;

        const int new_w =
            std::max(1, static_cast<int>(std::lround(w * applied_scale)));
        const int new_h =
            std::max(1, static_cast<int>(std::lround(h * applied_scale)));

        pixels = ScaleCursorPixels(pixels, w, h, new_w, new_h);

        w = new_w;
        h = new_h;
    }

    GLFWimage image;
    image.width = w;
    image.height = h;
    image.pixels = pixels.data();

    const int hotX =
        (def.hotspotX < 0)
            ? w / 2
            : static_cast<int>(std::lround(def.hotspotX * applied_scale));

    const int hotY =
        (def.hotspotY < 0)
            ? h / 2
            : static_cast<int>(std::lround(def.hotspotY * applied_scale));

    GLFWcursor* cursor =
        glfwCreateCursor(&image, hotX, hotY);

    if (!cursor)
        std::cerr << "[CURSOR] glfwCreateCursor failed for " << def.file << "\n";

    return cursor;
}

static void LoadCustomCursor(GLFWwindow* win)
{
    cursorWindow = win;

    GLFWcursor* loaded[kCursorKindCount] = {};
    bool any_loaded = false;

    for (int i = 0; i < kCursorKindCount; ++i)
    {
        loaded[i] = CreateCursorFromImageFile(kCursorDefs[i]);

        if (loaded[i])
            any_loaded = true;
        else
            std::cerr << "[CURSOR] " << kCursorDefs[i].file
                      << " not found, default cursor used\n";
    }

    // Nothing found : keep the system cursors (and let ImGui manage them).
    if (!any_loaded)
        return;

    for (int i = 0; i < kCursorKindCount; ++i)
    {
        if (loaded[i])
            cursors[i] = loaded[i];
        else if (kCursorDefs[i].fallbackShape != 0)
            cursors[i] = glfwCreateStandardCursor(kCursorDefs[i].fallbackShape);
    }

    customCursorsEnabled = true;

    SetCursorKind(kCursorArrow);
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

    int win_w = 0;
    int win_h = 0;

    glfwGetWindowSize(glfwGetCurrentContext(), &win_w, &win_h);

    scene_x = (window_x - sceneViewport.x) * win_w / sceneViewport.w;
    scene_y = (window_y - sceneViewport.y) * win_h / sceneViewport.h;
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
    HRL_SetViewportCamera(
        viewport,
        camera
    );

    isPlaying = playing;

    if (playing)
        engine->StartGame();
    else
        engine->EndGame();
}


static std::string world_file_path_string;


#ifdef LYNX_EDITOR

// =============================================================================
// EDITOR ONLY : everything below, up to the matching #else, is compiled only
// when LYNX_EDITOR is defined.
// =============================================================================


void InitImGui(GLFWwindow* window)
{
    IMGUI_CHECKVERSION();

    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // ImGui's GLFW backend calls glfwSetCursor(arrow) every frame, which would
    // erase our image. We pick the cursor ourselves (UpdateEditorMouseCursor).
    if (customCursorsEnabled)
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    io.Fonts->AddFontFromFileTTF(
        "normal-font.ttf",
        24.0f
    );

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(
        window,
        true
    );

    ImGui_ImplOpenGL3_Init(
        "#version 330"
    );
}


void BeginImGuiFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Main dockspace: every editor panel can be docked/rearranged by ImGui.
    ImGui::DockSpaceOverViewport(
        0,
        ImGui::GetMainViewport(),
        ImGuiDockNodeFlags_PassthruCentralNode
    );
}


void EndImGuiFrame()
{
    ImGui::Render();

    ImGui_ImplOpenGL3_RenderDrawData(
        ImGui::GetDrawData()
    );
}


void ShutdownImGui()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}


float camX;
float camY;
float camZ = 200.f;

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

    snapshot.type_name = lynx::ETypeName(*actor);
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
    actor->transform = snapshot.transform;

    auto& properties = actor->GetProperties();

    for (const auto& [name, value] : snapshot.properties)
    {
        auto it = properties.find(name);

        if (it != properties.end())
            ApplyEditorProperty(it->second, value);
    }

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

    auto* factory = lynx::GetEngine()->GetFactory().GetInternalFactory();

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

    if (actor_types.empty())
    {
        ImGui::TextDisabled("No matching actors");
    }
    else
    {
        for (const std::string& type_name : actor_types)
        {
            if (ImGui::Button(
                    type_name.c_str(),
                    ImVec2(-1.0f, 0.0f)
                ))
            {
                lynx::Actor* actor =
                    level->SpawnActor(type_name.c_str());

                if (actor)
                {
                    AssignDefaultActorName(level, actor, type_name);

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

                    SetActorSelected(actor);
                    MarkEditorDirty();
                }
            }
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

    BusyCursorScope busy_cursor;

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


static void DrawContentBrowser(lynx::Level* level)
{
    if (!level)
        return;

    ImGui::Begin("Content Browser");
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

    if (ImGui::Button("Assets"))
    {
        content_browser_current_path =
            content_browser_root;
    }

    std::filesystem::path breadcrumb_path;
    int breadcrumb_index = 0;

    for (const auto& part : relative_current)
    {
        breadcrumb_path /= part;

        ImGui::SameLine();
        ImGui::TextUnformatted(">>");
        ImGui::SameLine();

        std::string label = part.string();
        label += "##Breadcrumb";
        label += std::to_string(breadcrumb_index++);

        if (ImGui::Button(label.c_str()))
        {
            content_browser_current_path =
                content_browser_root / breadcrumb_path;
        }
    }

    ImGui::Separator();

    if (content_browser_current_path != content_browser_root &&
        ImGui::Button(".."))
    {
        const std::filesystem::path parent =
            content_browser_current_path.parent_path();

        if (IsPathInside(parent, content_browser_root))
            content_browser_current_path = parent;
    }

    ImGui::SameLine();
    ImGui::TextDisabled(
        "%s",
        content_browser_current_path.generic_string().c_str()
    );

    ImGui::Separator();

    std::vector<std::filesystem::directory_entry> entries;
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

    int column = 0;

    for (const auto& entry : entries)
    {
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

        // Button background.
        const ImU32 bg_color =
            active ? IM_COL32(75, 75, 75, 255) :
            hovered ? IM_COL32(58, 58, 58, 255) :
                      IM_COL32(43, 43, 43, 255);

        draw_list->AddRectFilled(
            cell_min,
            cell_max,
            bg_color,
            6.f
        );

        draw_list->AddRect(
            cell_min,
            cell_max,
            hovered ? IM_COL32(120, 120, 120, 255)
                    : IM_COL32(70, 70, 70, 255),
            6.f,
            0,
            1.f
        );

        // Thumbnail / asset icon.
        if (is_directory)
        {
            // Folder icon.
            const ImU32 folder_color = IM_COL32(218, 177, 70, 255);
            const ImU32 folder_top = IM_COL32(236, 196, 88, 255);

            draw_list->AddRectFilled(
                ImVec2(thumb_min.x + 8.f, thumb_min.y + 25.f),
                ImVec2(thumb_max.x - 8.f, thumb_max.y - 12.f),
                folder_color,
                7.f
            );

            draw_list->AddRectFilled(
                ImVec2(thumb_min.x + 13.f, thumb_min.y + 17.f),
                ImVec2(thumb_min.x + 48.f, thumb_min.y + 32.f),
                folder_top,
                5.f
            );
        }
        else
        {
            // Generic file thumbnail with a small extension badge.
            const ImU32 file_color = IM_COL32(125, 145, 175, 255);
            const ImU32 paper_color = IM_COL32(205, 210, 218, 255);

            const ImVec2 paper_min(
                thumb_min.x + 22.f,
                thumb_min.y + 8.f
            );
            const ImVec2 paper_max(
                thumb_max.x - 22.f,
                thumb_max.y - 8.f
            );

            draw_list->AddRectFilled(
                paper_min,
                paper_max,
                paper_color,
                6.f
            );

            // Folded corner.
            draw_list->AddTriangleFilled(
                ImVec2(paper_max.x - 24.f, paper_min.y),
                ImVec2(paper_max.x, paper_min.y + 24.f),
                ImVec2(paper_max.x - 24.f, paper_min.y + 24.f),
                file_color
            );

            // Asset-type accent based on extension.
            std::string extension =
                entry.path().extension().string();

            std::transform(
                extension.begin(),
                extension.end(),
                extension.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::toupper(c));
                }
            );

            if (extension.size() > 5)
                extension.resize(5);

            const ImVec2 badge_min(
                paper_min.x + 8.f,
                paper_max.y - 28.f
            );
            const ImVec2 badge_max(
                paper_max.x - 8.f,
                paper_max.y - 8.f
            );

            draw_list->AddRectFilled(
                badge_min,
                badge_max,
                file_color,
                4.f
            );

            if (!extension.empty())
            {
                const ImVec2 text_size =
                    ImGui::CalcTextSize(extension.c_str());

                draw_list->AddText(
                    ImVec2(
                        badge_min.x +
                            (badge_max.x - badge_min.x - text_size.x) * 0.5f,
                        badge_min.y +
                            (badge_max.y - badge_min.y - text_size.y) * 0.5f
                    ),
                    IM_COL32(255, 255, 255, 255),
                    extension.c_str()
                );
            }
        }

        // Asset name under the image, clipped/wrapped to the cell width.
        const ImVec2 text_size =
            ImGui::CalcTextSize(filename.c_str(), nullptr, false, item_width - 8.f);

        const float text_x =
            cell_pos.x + (item_width - std::min(text_size.x, item_width - 8.f)) * 0.5f;

        draw_list->AddText(
            ImGui::GetFont(),
            ImGui::GetFontSize(),
            ImVec2(text_x, cell_pos.y + image_size + 9.f),
            IM_COL32(235, 235, 235, 255),
            filename.c_str(),
            nullptr,
            item_width - 8.f
        );

        if (clicked && is_directory)
            content_browser_current_path = entry.path();

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

        ImGui::PopID();

        ++column;

        if (column >= columns)
            column = 0;
    }

    if (entries.empty())
        ImGui::TextDisabled("Empty folder");

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

// Editor camera height limits and speed scaling.
// The "Camera Speed" slider is the speed at kCameraSpeedRefZ ; the real speed
// is proportional to the camera height (far = faster, close = slower).
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

    for (int y = -brushRadius; y <= brushRadius; ++y)
    {
        for (int x = -brushRadius; x <= brushRadius; ++x)
        {
            if (x * x + y * y > brushRadius * brushRadius)
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


// ---------------------------------------------------------------------------
// Stroke interpolation : when the mouse moves faster than one brush radius
// per frame, the brush is stamped on every voxel of the line between the
// previous and the current position, so the stroke has no gaps.
// ---------------------------------------------------------------------------

static bool brushHasLastCenter = false;
static int brushLastCenterX = 0;
static int brushLastCenterY = 0;

static void stampBrush(int centerX, int centerY)
{
    for (int y = -brushRadius; y <= brushRadius; ++y)
    {
        for (int x = -brushRadius; x <= brushRadius; ++x)
        {
            if (x * x + y * y <= brushRadius * brushRadius)
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

    BusyCursorScope busy_cursor;

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

        // Editing tools are unavailable while the game is running.
        ImGui::BeginDisabled(isPlaying);

        if (ImGui::Button("Save"))
            SaveEditor();
        tooltip("Save the level (Ctrl+S)");

        ImGui::SameLine();

        if (ImGui::Button("Undo") && !brushPainting)
            UndoLastEditorAction();
        tooltip("Undo (Ctrl+Z)");

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        auto mode_button = [&](const char* label, int index, const char* tip)
        {
            const bool active = (gizmoModeIndex == index);

            if (active)
            {
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
                );
            }

            if (ImGui::Button(label))
                SetGizmoModeIndex(index);

            if (active)
                ImGui::PopStyleColor();

            tooltip(tip);
        };

        mode_button("Translate", 0, "Translate gizmo (Space cycles modes)");
        ImGui::SameLine();
        mode_button("Rotate", 1, "Rotate gizmo (Space cycles modes)");
        ImGui::SameLine();
        mode_button("Scale", 2, "Scale gizmo (Space cycles modes)");

        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Play / Stop stays available in both modes.
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            isPlaying
                ? ImVec4(0.70f, 0.20f, 0.20f, 1.0f)
                : ImVec4(0.20f, 0.55f, 0.25f, 1.0f)
        );

        if (ImGui::Button(isPlaying ? "Stop" : "Play"))
            TogglePlayMode();

        ImGui::PopStyleColor();
        tooltip("Toggle editor / game (F3)");

        // Reload the game DLL. Only a REQUEST is raised here : the actual reload
        // happens in main(), between two frames, never in the middle of the ImGui
        // frame (the panels are still holding actor pointers at this point).
        ImGui::SameLine();

        if (ImGui::Button("Reload Game"))
            reload_game_requested = true;

        tooltip(
            "Reload libGameExample.dll\n"
            "Stops the game, saves pending level edits, destroys every actor,\n"
            "unloads the DLL, loads it again and rebuilds the level."
        );

        // Master volume stays available in both modes.
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        float master_volume =
            lynx::GetMasterVolume();

        ImGui::SetNextItemWidth(
            ImGui::GetFontSize() * 8.f
        );

        if (ImGui::SliderFloat(
                "##MasterVolume",
                &master_volume,
                0.f,
                2.f,
                "Volume %.2f"
            ))
        {
            lynx::SetMasterVolume(
                master_volume
            );
        }

        tooltip("Master volume");

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
// Without LYNX_EDITOR they are empty inline stubs (see #else below).
// =============================================================================
// -----------------------------------------------------------------------------
// Editor camera persistence
// -----------------------------------------------------------------------------
// Stored next to the executable (working directory), NOT in assets/, so it is
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
#ifdef _WIN32

    using SetCollisionDebugEnabledFn =
        void (*)(bool);


    HMODULE game_dll =
        GetModuleHandleA(
            "libGameExample.dll"
        );


    if (!game_dll)
        return;


    auto set_collision_debug =
        reinterpret_cast<
            SetCollisionDebugEnabledFn
        >(
            GetProcAddress(
                game_dll,
                "Game_SetCollisionDebugEnabled"
            )
        );


    if (set_collision_debug)
    {
        set_collision_debug(
            show_collision_debug
        );
    }

#endif
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
            1000.f
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


        InitImGui(win);

        // The editor starts 0.5.
        lynx::SetMasterVolume(
            0.5f
        );


        // ------------------------------------------------------------
        // Gizmo
        // ------------------------------------------------------------

        gizmo =
            HRL_CreateGizmo(
                viewport
            );


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

        if (io.KeyCtrl && SceneAcceptsMouse(io.WantCaptureMouse))
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
        if (SceneAcceptsMouse(ImGui::GetIO().WantCaptureMouse))
        {
            camZ =
                ClampCameraZ(
                    camZ -
                    static_cast<float>(yoffset) * 10.f
                );
        }
    }


    // Picks the cursor image for this frame. ImGui tells which kind of cursor
    // it wants (arrow, text caret, resize...) ; we show our image for it.
    static void UpdateEditorMouseCursor(GLFWwindow* win)
    {
        static bool cursor_hidden = false;

        const ImGuiIO& io = ImGui::GetIO();
        const ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();

        const bool want_hidden =
            (imgui_cursor == ImGuiMouseCursor_None);

        if (want_hidden != cursor_hidden)
        {
            cursor_hidden = want_hidden;

            glfwSetInputMode(
                win,
                GLFW_CURSOR,
                cursor_hidden ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL
            );
        }

        if (cursor_hidden)
            return;

        int kind = kCursorArrow;

        switch (imgui_cursor)
        {
        case ImGuiMouseCursor_TextInput: kind = kCursorText;       break;
        case ImGuiMouseCursor_ResizeEW:  kind = kCursorResizeEW;   break;
        case ImGuiMouseCursor_ResizeNS:  kind = kCursorResizeNS;   break;
        case ImGuiMouseCursor_ResizeNWSE:kind = kCursorResizeNWSE; break;
        case ImGuiMouseCursor_ResizeNESW:kind = kCursorResizeNESW; break;
        case ImGuiMouseCursor_Hand:      kind = kCursorSelect;     break;
        default:                         break;
        }

        if (busyCursorDepth > 0)
        {
            kind = kCursorLoading;
        }
        else if (kind == kCursorArrow &&
                 !isPlaying &&
                 io.KeyCtrl &&
                 SceneAcceptsMouse(io.WantCaptureMouse))
        {
            // Ctrl + click selects an object in the viewport.
            kind = kCursorSelect;
        }

        SetCursorKind(kind);
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


        if (customCursorsEnabled)
            UpdateEditorMouseCursor(win);


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


        if (ctrl_s && !ctrl_s_was_down)
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

            if (!io.KeyCtrl &&
                SceneAcceptsMouse(io.WantCaptureMouse))
            {
                const bool leftMousePressed =
                    glfwGetMouseButton(
                        win,
                        GLFW_MOUSE_BUTTON_LEFT
                    ) == GLFW_PRESS;


                if (brushPaintEnabled &&
                    leftMousePressed &&
                    !brushPainting)
                {
                    brushPainting = true;

                    brushHasLastCenter = false;

                    brushPaintedVoxels.clear();

                    currentStrokeChanges.clear();
                }


                if ((!leftMousePressed ||
                     !brushPaintEnabled) &&
                    brushPainting)
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


                if (brushPaintEnabled &&
                    leftMousePressed)
                {
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
                else if (brushPaintEnabled &&
                         !leftMousePressed &&
                         !isPlaying)
                {
                    // Hover : preview the brush on the voxels.
                    int centerX;
                    int centerY;

                    if (HRL_GetVoxelAtScreenPosition(
                            scene,
                            static_cast<int>(mouseX),
                            static_cast<int>(mouseY),
                            &centerX,
                            &centerY))
                    {
                        ApplyBrushPreview(centerX, centerY);
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
                    BusyCursorScope busy_cursor;

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
        return;

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
                sceneViewport.hovered =
                    ImGui::IsItemHovered() || ImGui::IsItemActive();
                sceneViewport.x = pos.x;
                sceneViewport.y = pos.y;
                sceneViewport.w = size.x;
                sceneViewport.h = size.y;
            }
        }

        ImGui::End();
    }


    void DrawUI(GLFWwindow* win)
    {
        lynx::Level* level = editor_level;

        BeginImGuiFrame();

        DrawViewportWindow();

        DrawToolbar();

        ImGuiIO& io =
            ImGui::GetIO();




        // --------------------------------------------------------
        // Undo
        // --------------------------------------------------------

        if (!isPlaying &&
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

        if (!isPlaying)
        {
            ImGui::Begin("Outliner");

            const auto& actors = level->GetActors();

            if (actors.empty())
            {
                ImGui::TextDisabled("No actors");
            }
            else
            {
                for (size_t i = 0; i < actors.size(); ++i)
                {
                    lynx::Actor* actor = actors[i];

                    if (!actor)
                        continue;

                    std::string typeName =
                        lynx::ETypeName(*actor);

                    std::string label = typeName;

                    if (!actor->object_id_.empty())
                    {
                        label += " [" + actor->object_id_ + "]";
                    }

                    label += "##OutlinerActor";
                    label += std::to_string(i);

                    const bool selected =
                        editing_actor == actor;

                    if (ImGui::Selectable(
                            label.c_str(),
                            selected
                        ))
                    {
                        SetActorSelected(actor);
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
                }
            }

            ImGui::End();


            // --------------------------------------------------------
            // Place Actors
            // --------------------------------------------------------

            PlaceActorsWindow(level);


            // --------------------------------------------------------
            // Content Browser
            // --------------------------------------------------------

            DrawContentBrowser(level);


            // --------------------------------------------------------
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
                    lynx::ETypeName(*editing_actor);

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

                ImGui::Text("Properties");

                const auto& properties =
                    editing_actor->GetProperties();

                if (properties.empty())
                {
                    ImGui::TextDisabled(
                        "No properties"
                    );
                }
                else
                {
                    for (const auto& [name, prop] : properties)
                    {
                        // object_id_ is edited separately above.
                        if (name == "object_id_")
                            continue;

                        ImGui::PushID(name.c_str());

                            ImGui::Text("%s", name.c_str());
                            ImGui::SameLine();

                            bool changed = false;

                            switch (prop.GetType())
                            {
                                case 0: // int
                                {
                                    if (auto ptr =
                                            std::get_if<int*>(&prop.property_member))
                                    {
                                        if (*ptr)
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

                                            if (ImGui::DragFloat2(
                                                    "##value",
                                                    values,
                                                    0.05f
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

                                            if (ImGui::DragFloat3(
                                                    "##value",
                                                    values,
                                                    0.05f
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

                                            if (ImGui::DragFloat4(
                                                    "##value",
                                                    values,
                                                    0.05f
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
        // Color Picking
        // --------------------------------------------------------

        ImGui::Begin(
            "Color Picking"
        );


        ImVec2 viewportSize =
            ImGui::GetContentRegionAvail();


        float col[4] =
        {
            1.f,
            1.f,
            1.f,
            1.f
        };


        ImGui::ColorEdit4(
            "Couleur",
            col,
            ImGuiColorEditFlags_Float
        );


        ImGui::Text("");


        ImGui::End();


        // --------------------------------------------------------
        // Config
        // --------------------------------------------------------

        ImGui::Begin("Config");


        ImGui::SliderFloat(
            "Camera Speed",
            &cameraSpeed,
            1.f,
            400.f
        );


        // --------------------------------------------------------
        // Brush Mode
        // --------------------------------------------------------

        ImGui::Text(
            "Brush Mode"
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
                20
            );


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
            // Empty voxel
            // ----------------------------------------------------

            ImGui::PushID(0);


            if (ImGui::ColorButton(
                    "##VoxelEmpty",
                    ImVec4(
                        0.f,
                        0.f,
                        0.f,
                        1.f
                    ),
                    ImGuiColorEditFlags_NoTooltip,
                    ImVec2(
                        40.f,
                        40.f
                    )
                ))
            {
                brushVoxelType = 0;
            }


            if (brushVoxelType == 0)
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

            ImGui::PopID();


            // ----------------------------------------------------
            // Voxel types
            // ----------------------------------------------------

            for (int i = 0; i < voxelDataCount; ++i)
            {
                int type = i + 1;


                ImVec4 color(
                    voxelData[i].color[0],
                    voxelData[i].color[1],
                    voxelData[i].color[2],
                    voxelData[i].color[3]
                );


                ImGui::PushID(type);


                if (ImGui::ColorButton(
                        "##VoxelColor",
                        color,
                        ImGuiColorEditFlags_NoTooltip,
                        ImVec2(
                            40.f,
                            40.f
                        )
                    ))
                {
                    brushVoxelType = type;
                }


                if (brushVoxelType == type)
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


                if ((i + 1) % 8 != 0)
									ImGui::SameLine();


                ImGui::PopID();
            }
        }


        ImGui::End();


        // --------------------------------------------------------
        // Camera Shake
        // --------------------------------------------------------

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
        // Level drag & drop
        // --------------------------------------------------------

        HandleLevelAssetDrop(level);


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
            if (editor_dirty)
            {
                ImGui::TextUnformatted(
                    "The level has unsaved changes."
                );
                ImGui::TextUnformatted(
                    "Do you want to save before quitting?"
                );
                ImGui::Separator();

                if (ImGui::Button("Save and Quit"))
                {
                    SaveEditor();
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
                    ImGui::CloseCurrentPopup();
                }
            }

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


        EndImGuiFrame();
    }
}

#else  // !LYNX_EDITOR : game only

namespace editor
{
    constexpr bool kEnabled = false;

    inline HRL_id CreateCamera(HRL_id, HRL_id fallback) { return fallback; }
    inline void Init(GLFWwindow*, lynx::Engine*, HRL_id, HRL_id, lynx::Level*, const std::string&) {}
    inline void Shutdown() {}
    inline bool ConsumeReloadRequest() { return false; }
    inline bool ConsumeRestoreRequest() { return false; }
    inline void BeginRestoreAfterPlay() {}
    inline void RestoreVoxelsAfterPlay() {}
    inline void EndRestoreAfterPlay(lynx::Level*) {}
    inline void BeginGameReload() {}
    inline void EndGameReload(lynx::Level*) {}
    inline bool BlockGameInput() { return false; }
    inline bool WantsMouse() { return false; }
    inline void OnLeftMousePress(GLFWwindow*) {}
    inline void OnScroll(double) {}
    inline void Tick(GLFWwindow*, float) {}
    inline void DrawUI(GLFWwindow*) {}
}

#endif  // LYNX_EDITOR


static void FramebufferSizeCallback(
    GLFWwindow*,
    int width,
    int height)
{
    HRL_WindowResizeCallback(
        width,
        height
    );
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
    const bool blocked = editor::BlockGameInput();

    if (action == GLFW_PRESS && !blocked)
    {
        lynx::InjectMouseButtonDown(button);
    }
    else if (action == GLFW_RELEASE && !blocked)
    {
        lynx::InjectMouseButtonUp(button);
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


float gameplayCamX = 0.f;
float gameplayCamY = 0.f;


int main()
{
    lynx::Engine* engine =
        lynx::CreateEngine("", false);

    lynx::LoadConfigFile(
        "input.json"
    );

    // The game module is owned through a pointer so the editor can unload it and
    // load it again (toolbar "Reload Game"). Destroying it (reset / destructor)
    // invalidates everything that comes from the DLL : see SysModule::Unload().
    std::unique_ptr<lynx::SysModule> gameModule;

    auto load_game_module = [&]()
    {
        gameModule =
            std::make_unique<lynx::SysModule>(
                "libGameExample.dll"
            );

        gameModule->RegisterFactory();
    };

    load_game_module();


    glfwInit();

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


    GLFWwindow* win =
        glfwCreateWindow(
            1280,
            720,
            "Lynx Engine",
            nullptr,
            nullptr
        );

    glfwMaximizeWindow(win);

    glfwMakeContextCurrent(win);


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



    HRL_Init(HRL_OPENGL_33);

    int winX, winY;
    glfwGetWindowSize(win, &winX, &winY);

    HRL_InitContext(
        winX,
        winY,
        (void*)glfwGetProcAddress
    );

    HRL_RegisterErrorCallback(
        ErrorCallback
    );


    // Needs HRL (texture loading) : after HRL_InitContext, before editor::Init.
    LoadCustomCursor(win);

    HRL_SetDebugLineThickness(
        3.f
    );


    scene =
        HRL_CreateScene(true);

    lynx::SetSceneID(scene);


    HRL_id gameplay_cam =
        HRL_CreateCamera(
            scene,
            HRL_PERSPECTIVE
        );


    HRL_SetCameraPerspectiveFov(
        gameplay_cam,
        20.f
    );

    HRL_SetCameraRotation(
        gameplay_cam,
        0.f,
        -90.f,
        0.f
    );


    // ------------------------------------------------------------
    // Load world
    // ------------------------------------------------------------

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

        HRL_SetVoxelPhysicalSize(
            scene,
            0.15f
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


    auto* level = engine->CreateLevel(
        "world.xml"
    );

    auto font_data =
        lynx::fs::ReadBinary(
            "normal-font.ttf"
        );


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


    // ------------------------------------------------------------
    // Viewport
    // ------------------------------------------------------------
    // Editor build : starts on the editor camera.
    // Game build   : starts directly on the gameplay camera.

    HRL_id start_camera =
        editor::CreateCamera(
            scene,
            gameplay_cam
        );


    HRL_id viewport =
        HRL_CreateViewport(
            scene,
            start_camera,
            0.f,
            0.f,
            1.f,
            1.f
        );
    lynx::SetViewportID(viewport);


    editor::Init(
        win,
        engine,
        viewport,
        gameplay_cam,
        level,
        save_data
    );


    for (int i = 0; i < voxelDataCount; ++i)
    {
        int type = i + 1;

        HRL_SetVoxelTypeColor(
            scene,
            type,
            voxelData[i].color[0],
            voxelData[i].color[1],
            voxelData[i].color[2],
            voxelData[i].color[3]
        );

        HRL_SetVoxelTypeCollisionFlags(
            scene,
            type,
            voxelData[i].flags
        );

        if (voxelData[i].is_emissive)
        {
            HRL_SetVoxelTypeEmissiveColor(
                scene,
                type,
                voxelData[i].emissive_color[0],
                voxelData[i].emissive_color[1],
                voxelData[i].emissive_color[2]
            );
        }
    }


    HRL_id post_mat =
        HRL_CreateMaterial(
            HRL_DEFAULT_POST_PROCESS_SHADER
        );


    HRL_id post =
        HRL_CreatePostProcess(
            viewport,
            post_mat,
            1
        );


    HRL_MaterialSetFloat(
        post_mat,
        "vignetteStrength",
        0.4f
    );


    // Background music : game build only, the editor stays silent.
#ifndef LYNX_EDITOR
    lynx::Audio2D ambient_cave(
        "cave_ambience.mp3"
    );

    ambient_cave.looping = true;
    ambient_cave.Play();
#endif


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


    auto* player =
        lynx::GetEngine()
            ->GetCurrentLevel()
            ->GetActorFromID("Pawn");


    gameplayCamX =
        player->transform.location.x;


    gameplayCamY =
        player->transform.location.y;


    HRL_SetCameraLocation(
        gameplay_cam,
        gameplayCamX,
        gameplayCamY,
        80.f
    );


    // ------------------------------------------------------------
    // Game module reload (editor toolbar "Reload Game")
    // ------------------------------------------------------------
    // Returns false when the game cannot go on (the window is then closed).

    auto reload_game_module = [&]() -> bool
    {
        BusyCursorScope busy_cursor;

        std::cout << "[RELOAD] Reloading libGameExample.dll\n";

        // 1. Editor side : stops the game, saves pending edits, drops every actor
        //    pointer / selection / undo entry.
        editor::BeginGameReload();

        player = nullptr;

        // 2. Unload the DLL. SysModule::Unload() first calls
        //    engine->DeleteCurrentLevel() (every actor dies while their code is
        //    still mapped), then unregisters the factory, then FreeLibrary.
        //    `level` is dangling from here until CreateLevel() below.
        gameModule.reset();

        level = nullptr;

        // 3. Load the DLL again and register its actor classes.
        load_game_module();

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

        player =
            level->GetActorFromID(
                "Pawn"
            );

        if (!player)
        {
            std::cerr << "[RELOAD] ERROR: no \"Pawn\" actor after reload.\n";
            return false;
        }

        gameplayCamX =
            player->transform.location.x;

        gameplayCamY =
            player->transform.location.y;

        editor::EndGameReload(level);

        std::cout << "[RELOAD] Done\n";

        return true;
    };


    // ------------------------------------------------------------
    // Main loop
    // ------------------------------------------------------------

    // Game-only build : there is no F3 toggle, the game just runs.
    if (!editor::kEnabled)
    {
        SetPlaying(engine, viewport, gameplay_cam, true);
    }


    while (!glfwWindowShouldClose(win))
    {
        glfwPollEvents();


        // --------------------------------------------------------
        // Game DLL reload : between two frames, no ImGui frame is open
        // --------------------------------------------------------

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

            player = nullptr;

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

            player =
                level->GetActorFromID(
                    "Pawn"
                );

            if (!player)
            {
                std::cerr << "[PLAY] ERROR: no \"Pawn\" actor after restore.\n";

                glfwSetWindowShouldClose(
                    win,
                    GLFW_TRUE
                );

                continue;
            }

            gameplayCamX =
                player->transform.location.x;

            gameplayCamY =
                player->transform.location.y;

            editor::EndRestoreAfterPlay(level);

            lastFrameTime =
                glfwGetTime();
        }


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


            char title[128];


            std::snprintf(
                title,
                sizeof(title),
                "Lynx Engine - %.0f FPS",
                fps
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

        editor::Tick(
            win,
            dt
        );


        // --------------------------------------------------------
        // Engine / HRL
        // --------------------------------------------------------
        lynx::gamepad::Poll(editor::BlockGameInput());
        engine->ProgressOneFrame(dt);


        // --------------------------------------------------------
        // Gameplay camera
        // --------------------------------------------------------

        float targetX =
            player->transform.location.x;


        float targetY =
            player->transform.location.y;


        float cameraFollowSpeed = 3.f;


        float follow =
            1.f -
            std::exp(
                -cameraFollowSpeed * dt
            );


        gameplayCamX +=
            (targetX - gameplayCamX) *
            follow;


        gameplayCamY +=
            (targetY - gameplayCamY) *
            follow;


        float shakenX =
            gameplayCamX;


        float shakenY =
            gameplayCamY + 2.f;


        float shakenRotationZ =
            0.f;


        lynx::GetCameraShake().Update(
            dt,
            shakenX,
            shakenY,
            shakenRotationZ
        );


        HRL_SetCameraLocation(
            gameplay_cam,
            shakenX,
            shakenY,
            80.f
        );


        HRL_SetCameraRotation(
            gameplay_cam,
            0.f,
            -90.f,
            shakenRotationZ
        );


        // --------------------------------------------------------
        // Editor UI (ImGui)
        // --------------------------------------------------------

        editor::DrawUI(win);


        glfwSwapBuffers(win);
    }


    // ------------------------------------------------------------
    // Shutdown
    // ------------------------------------------------------------

    // The module needs a live engine to invalidate its actors / factory.
    gameModule.reset();

    delete lynx::GetEngine();

    editor::Shutdown();


    return 0;
}
