// =============================================================================
// Lynx Runtime
// -----------------------------------------------------------------------------
// Plays a game project without the editor :
//
//   LynxRuntime.exe                    -> project = working directory
//   LynxRuntime.exe "C:/path/MyGame"   -> project = this folder
//
// The project root holds assets/ (and input.json, settings.cfg). The game DLL
// is looked for next to this executable first (shipped game), then in the
// project's build/ folder (development).
//
// Everything specific to the game comes from its DLL (core/GameModuleAPI.h)
// and from assets/voxels.json (core/Voxels.h).
// =============================================================================

#include "../Lynx.h"
#include "../core/GameModuleAPI.h"
#include <glfw/glfw3.h>
#include <hrl/hrl.h>
#include <hrl/hrl_gl.h>
#define STB_IMAGE_IMPLEMENTATION
#include "../../third-party/stb/stb_image.h"

#include <iostream>
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <cstdint>
#ifdef _WIN32
#include <io.h>
#endif

#include "../audio/AudioCommon.h"
#include "../core/Private/SystemModule.h"
#include "../gameplay/Private/InputManager.h"
#include "../host/GamepadInput.h"
#include "../host/GameProject.h"

static HRL_id scene;

// Optional functions exported by the game DLL.
static lynx::game_api::Hooks gameHooks;


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


// -----------------------------------------------------------------------------
// Settings (settings.cfg, written by the editor in the project folder)
// -----------------------------------------------------------------------------

static const char* const kSettingsFile = "settings.cfg";

struct AppSettings
{
    bool vsync = false;
    float masterVolume = 0.5f;
    float cameraSpeed = 50.f;
    HRL_EVoxelRenderMode voxelRenderMode = HRL_VOXEL_BLOCKY;
    float voxelPhysicalSize = 0.3f;
};

static AppSettings appSettings;

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
            float value = appSettings.voxelPhysicalSize;
            if (file >> value && std::isfinite(value))
                appSettings.voxelPhysicalSize =
                    std::clamp(value, 0.001f, 100.f);
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


// -----------------------------------------------------------------------------
// Input callbacks : everything goes to the game
// -----------------------------------------------------------------------------

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
    HRL_MouseMovedCallback(
        static_cast<float>(x),
        static_cast<float>(y)
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
    GLFWwindow*,
    int button,
    int action,
    int)
{
    if (action == GLFW_PRESS)
        lynx::InjectMouseButtonDown(button);
    else if (action == GLFW_RELEASE)
        lynx::InjectMouseButtonUp(button);

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


static void ScrollCallback(
    GLFWwindow*,
    double,
    double yoffset)
{
    lynx::InjectMouseWheel(
        static_cast<float>(yoffset)
    );
}


static void KeyCallback(
    GLFWwindow*,
    int key,
    int,
    int action,
    int)
{
    if (action == GLFW_PRESS)
        lynx::InjectKeyDown(key);
    else if (action == GLFW_RELEASE)
        lynx::InjectKeyUp(key);
}


// Game DLL : next to the executable (shipped game), else in <project>/build.
static std::filesystem::path FindGameModule(const std::filesystem::path& project_root)
{
    auto modules = lynx::host::FindGameModules(lynx::host::GetEditorDirectory(), 0);

    if (modules.empty())
        modules = lynx::host::FindGameModules(project_root / "build", 1);

    return modules.empty() ? std::filesystem::path() : modules.front();
}


#ifdef _WIN32
// Shipped game (no console) : a crash writes where it happened in <Game>.log,
// next to the output of the game.
static LONG WINAPI ShippedCrashHandler(EXCEPTION_POINTERS* info)
{
    const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    void* address = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;

    char module_name[MAX_PATH] = "?";
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(address), &module) && module)
        GetModuleFileNameA(module, module_name, MAX_PATH);

    std::fprintf(stderr, "\n[CRASH] exception 0x%08lX at %p in %s (+0x%llX)\n",
                 static_cast<unsigned long>(code), address, module_name,
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module)));
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif


int main(int argc, char** argv)
{
    // ------------------------------------------------------------
    // Project : first argument, or the working directory
    // ------------------------------------------------------------

    std::error_code path_error;

    // Shipped game (editor > Ship Game) : the assets are packed in this
    // executable ; the game DLL, the engine DLLs and input.json are next to it.
    const bool shipped = lynx::fs::HasEmbeddedArchive();

    if (shipped)
    {
        std::filesystem::current_path(lynx::host::GetEditorDirectory(), path_error);
        lynx::Engine::SetReleaseMode(true);

#ifdef _WIN32
        // No console : everything printed goes to <Game>.log next to the game,
        // a crash too (where it happened).
        wchar_t exe_path[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) > 0)
        {
            const std::filesystem::path log = std::filesystem::path(exe_path).replace_extension(".log");
            if (_wfreopen(log.wstring().c_str(), L"w", stdout))
            {
                _dup2(_fileno(stdout), _fileno(stderr));
                std::setvbuf(stdout, nullptr, _IONBF, 0);
                std::setvbuf(stderr, nullptr, _IONBF, 0);
            }
        }
        SetUnhandledExceptionFilter(ShippedCrashHandler);
#endif
        std::cout << "[RUNTIME] Shipped game : assets read from the executable\n";
    }
    else if (argc > 1 && argv[1] && argv[1][0] != '\0')
    {
        std::filesystem::current_path(argv[1], path_error);

        if (path_error)
        {
            std::cerr << "[RUNTIME] Could not open the project folder " << argv[1]
                      << " : " << path_error.message() << "\n";
            return 1;
        }
    }

    const std::filesystem::path project_root =
        std::filesystem::current_path(path_error);

    if (!shipped && !std::filesystem::is_directory(project_root / "assets", path_error))
    {
        std::cerr << "[RUNTIME] No assets/ folder in " << project_root.string()
                  << " (run it from the project folder, or give the folder as argument)\n";
        return 1;
    }

    const std::filesystem::path game_module_path = FindGameModule(project_root);

    if (game_module_path.empty())
    {
        std::cerr << "[RUNTIME] No game DLL (exporting FactoryRegisterClasses) next to the "
                     "executable or in " << (project_root / "build").string() << "\n";
        return 1;
    }

    std::cout << "[RUNTIME] Project  : " << project_root.string() << "\n"
              << "[RUNTIME] Game DLL : " << game_module_path.string() << "\n";

    LoadSettings();

    // Release mode (packed assets) : lynx::Engine::SetReleaseMode(true) before.
    lynx::Engine* engine =
        lynx::Engine::Create();

    // Key bindings : input.json at the project root (working directory).
    lynx::LoadConfigFile(
        "input.json"
    );

    lynx::SysModule gameModule(game_module_path.string().c_str());

    if (!gameModule.IsLoaded())
    {
        std::cerr << "[RUNTIME] Could not load " << game_module_path.string() << "\n";
        return 1;
    }

    gameModule.RegisterFactory();
    gameHooks.Load(gameModule);

    lynx::SetMasterVolume(
        appSettings.masterVolume
    );


    glfwInit();

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Window title : the project (folder) name.
    // Window title : the executable name for a shipped game, else the project folder.
    std::string window_title = shipped
        ? lynx::host::GetEditorDirectory().filename().string()
        : project_root.filename().string();

    if (shipped)
    {
#ifdef _WIN32
        wchar_t exe_path[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) > 0)
            window_title = std::filesystem::path(exe_path).stem().string();
#endif
    }

    if (window_title.empty())
        window_title = "Lynx Engine";

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
        std::cerr << "[WINDOW] Could not create the window\n";
        return 1;
    }

    glfwMaximizeWindow(win);

    glfwMakeContextCurrent(win);

    glfwSwapInterval(
        appSettings.vsync ? 1 : 0
    );

    const auto iconFile = lynx::fs::ReadBinary("icon.png");
    if (!iconFile.empty() && iconFile.size() <= static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        int iconWidth = 0;
        int iconHeight = 0;
        stbi_uc* iconPixels = stbi_load_from_memory(
            iconFile.data(), static_cast<int>(iconFile.size()),
            &iconWidth, &iconHeight, nullptr, 4);
        if (iconPixels)
        {
            GLFWimage icon{ iconWidth, iconHeight, iconPixels };
            glfwSetWindowIcon(win, 1, &icon);
            stbi_image_free(iconPixels);
        }
    }

    glfwSetFramebufferSizeCallback(win, FramebufferSizeCallback);
    glfwSetMouseButtonCallback(win, MouseButtonCallback);
    glfwSetCursorPosCallback(win, MouseMove);
    glfwSetScrollCallback(win, ScrollCallback);
    glfwSetKeyCallback(win, KeyCallback);


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

    // Needs HRL (texture loading) : after HRL_InitContext.
    LoadCustomCursor(win);

    HRL_SetDebugLineThickness(
        3.f
    );


    // The scene belongs to the engine (Engine::GetScene()) : it needs the
    // HRL context, created just above.
    scene =
        engine->CreateScene();


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
    // World : assets/save_file.txt -> path of the voxel world in assets/
    // ------------------------------------------------------------

    auto save_file =
        lynx::fs::ReadBinary(
            "save_file.txt"
        );

    std::string world_path(
        reinterpret_cast<const char*>(save_file.data()),
        save_file.size()
    );

    while (!world_path.empty() &&
           (world_path.back() == '\n' ||
            world_path.back() == '\r' ||
            world_path.back() == ' ' ||
            world_path.back() == '\t'))
    {
        world_path.pop_back();
    }

    if (world_path.empty())
        world_path = "world.vox";

    const auto world_file =
        lynx::fs::ReadBinary(
            world_path
        );

    if (world_file.empty())
    {
        std::cerr << "[WORLD] ERROR: could not read " << world_path << "\n";
    }
    else
    {
        HRL_LoadVoxelWorldBuffer(
            scene,
            world_file.data(),
            world_file.size()
        );
    }


    // Voxel types (assets/voxels.json) : before the level, the actors may
    // need them when they are created.
    lynx::voxels::LoadFile("voxels.json");
    lynx::voxels::ApplyToScene(scene);

    // The game registers its voxel events...
    if (gameHooks.setup_scene)
        gameHooks.setup_scene(scene);


    engine->CreateLevel(
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
                reinterpret_cast<const char*>(font_data.data()),
                font_data.size()
            );

        HRL_SetDebugMeshInfoTextSize(scene, 32.f);
        HRL_SetDebugMeshInfoFont(scene, hrlfont);
        HRL_SetDebugMeshInfoTextColor(scene, 1, 0.2, 0.2, 1.0);
    }


    // One unit everywhere : 1 world unit = 1 voxel.
    HRL_SetVoxelPhysicalSize(scene, 1.f);


    HRL_id viewport =
        HRL_CreateViewport(
            scene,
            gameplay_cam,
            0.f,
            0.f,
            1.f,
            1.f
        );

    lynx::SetViewportID(viewport);


    HRL_SetVoxelRenderMode(
        scene,
        appSettings.voxelRenderMode
    );


    HRL_id post_mat =
        HRL_CreateMaterial(
            HRL_DEFAULT_POST_PROCESS_SHADER
        );

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


    HRL_BeginVoxelEdit(scene);
    HRL_EndVoxelEdit(scene);


    if (gameHooks.on_level_loaded)
        gameHooks.on_level_loaded(gameplay_cam);


    // ------------------------------------------------------------
    // Main loop
    // ------------------------------------------------------------

    engine->StartGame();

    if (gameHooks.on_game_start)
        gameHooks.on_game_start();

    double fpsTimer = glfwGetTime();
    int fpsFrames = 0;
    double lastFrameTime = glfwGetTime();

    while (!glfwWindowShouldClose(win))
    {
        glfwPollEvents();

        const double currentFrameTime = glfwGetTime();

        float dt = static_cast<float>(currentFrameTime - lastFrameTime);
        lastFrameTime = currentFrameTime;
        dt = std::min(dt, 0.1f);

        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // FPS counter
        fpsFrames++;
        const double now = glfwGetTime();

        if (now - fpsTimer >= 0.25)
        {
            const double fps = static_cast<double>(fpsFrames) / (now - fpsTimer);

            char title[256];
            std::snprintf(title, sizeof(title), "%s - %.0f FPS", window_title.c_str(), fps);
            glfwSetWindowTitle(win, title);

            fpsFrames = 0;
            fpsTimer = now;
        }

        lynx::gamepad::Poll(false);
        engine->ProgressOneFrame(dt);

        if (gameHooks.update_gameplay_camera)
        {
            LYNX_PROFILE_SCOPE("Game camera");
            gameHooks.update_gameplay_camera(gameplay_cam, dt);
        }

        {
            LYNX_PROFILE_SCOPE("Swap buffers");
            glfwSwapBuffers(win);
        }

        // Tracy : one frame (the runtime has no built-in Profiler window).
        LYNX_PROFILE_FRAME();
    }


    // ------------------------------------------------------------
    // Shutdown
    // ------------------------------------------------------------

    if (gameHooks.on_game_end)
        gameHooks.on_game_end();

    engine->EndGame();

    // The module needs a live engine to invalidate its actors / factory.
    gameHooks.Clear();
    gameModule.Unload();

    lynx::Engine::Destroy();

    return 0;
}
