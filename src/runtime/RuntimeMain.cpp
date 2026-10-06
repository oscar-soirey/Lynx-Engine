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
    // Renderer + widgets (DPI scale).
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

    // Icon chosen in Ship Game : written in the executable as "GLFW_ICON",
    // which GLFW uses for the window and the taskbar. assets/icon.png only
    // when the executable has none.
    bool exeHasIcon = false;
#ifdef _WIN32
    exeHasIcon = FindResourceW(nullptr, L"GLFW_ICON", MAKEINTRESOURCEW(14) /* RT_GROUP_ICON */) != nullptr;
#endif

    const auto iconFile = exeHasIcon ? std::vector<unsigned char>() : lynx::fs::ReadBinary("icon.png");
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

    // Size known by the widgets too.
    engine->SetRenderSize(winX, winY);

    HRL_RegisterErrorCallback(
        ErrorCallback
    );

    HRL_SetDebugLineThickness(
        3.f
    );


    // The scene belongs to the engine (Engine::GetScene()) : it needs the
    // HRL context, created just above.
    scene =
        engine->CreateScene();


    // The default player (player 0) got its viewport and its camera with the
    // scene : its camera is the gameplay camera given to the game hooks.
    lynx::PlayerController* defaultPlayer =
        engine->GetDefaultPlayer();

    HRL_id gameplay_cam =
        defaultPlayer->GetDefaultCamera();


    // ------------------------------------------------------------
    // Level : assets/save_file.txt -> startup .level, which is linked to its
    // voxel world (.hrlv). Old projects : save_file.txt = voxel world and
    // the actors in world.xml (see Level::ResolveStartupLevel).
    // ------------------------------------------------------------

    std::string legacy_voxel_world;
    const std::string level_file =
        lynx::Level::ResolveStartupLevel(&legacy_voxel_world);

    const std::string world_path =
        legacy_voxel_world.empty()
            ? lynx::Level::GetLinkedVoxelWorld(level_file)
            : legacy_voxel_world;

    std::cout << "[WORLD] Level " << level_file << ", voxel world " << world_path << "\n";

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
        level_file.c_str()
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


    // No viewport here : each PlayerController owns one (split-screen).
    defaultPlayer->SetViewCamera(gameplay_cam);


    HRL_SetVoxelRenderMode(
        scene,
        appSettings.voxelRenderMode
    );


    // Post process of every player (HRL default shader). Its settings come
    // from assets/postprocess.json (editor : Windows > Post Process).
    lynx::postprocess::Install();


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
