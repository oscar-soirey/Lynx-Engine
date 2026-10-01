#include <core/Engine.h>
#include <../src/Lynx.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include <glfw/glfw3.h>
#include <hrl/hrl.h>
#include <hrl/hrl_gl.h>

#include <iostream>
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <random>
#include <unordered_set>
#include <vector>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif

#include "Common.h"
#include "audio/AudioCommon.h"
#include "core/Private/SystemModule.h"
#include "gameplay/Private/InputManager.h"


float camX;
float camY;
float camZ = 200.f;

bool dragging_object = false;
bool isPlaying = false;


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


void InitImGui(GLFWwindow* window)
{
    IMGUI_CHECKVERSION();

    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    io.Fonts->AddFontFromFileTTF(
        "Ubuntu-Regular.ttf",
        32.0f
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


HRL_id scene;
HRL_id editing_object;
HRL_id gizmo;

lynx::Actor* editing_actor = nullptr;


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

        HRL_SetGizmoPosition(
            gizmo,
            editing_actor->transform.location.x,
            editing_actor->transform.location.y,
            editing_actor->transform.location.z
        );

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

    HRL_SetGizmoPosition(
        gizmo,
        editing_actor->transform.location.x,
        editing_actor->transform.location.y,
        editing_actor->transform.location.z
    );

    HRL_SetGizmoVisible(
        gizmo,
        HRL_TRUE
    );
}


static void MouseButtonCallback(
    GLFWwindow* window,
    int button,
    int action,
    int mods)
{
    auto io = ImGui::GetIO();

    if (action == GLFW_PRESS && !io.KeyShift)
    {
        lynx::InjectMouseButtonDown(button);
    }
    else if (action == GLFW_RELEASE && !io.KeyShift)
    {
        lynx::InjectMouseButtonUp(button);
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT &&
        action == GLFW_PRESS)
    {
        double mouseX;
        double mouseY;

        glfwGetCursorPos(
            window,
            &mouseX,
            &mouseY
        );

        if (!isPlaying)
        {
            auto io = ImGui::GetIO();

            if (io.KeyCtrl && !io.WantCaptureMouse)
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
    auto io = ImGui::GetIO();

    if (!io.KeyShift && !io.WantCaptureMouse)
    {
        lynx::InjectMouseWheel(
            static_cast<float>(yoffset)
        );
    }

    if (!io.WantCaptureMouse)
    {
        camZ -=
            static_cast<float>(yoffset) * 10.f;
    }
}


void inject_keys_callback(
    GLFWwindow* window,
    int key,
    int scancode,
    int action,
    int mods)
{
    auto io = ImGui::GetIO();

    if (action == GLFW_PRESS)
    {
        if (!io.KeyShift)
        {
            lynx::InjectKeyDown(key);
        }
    }
    else if (action == GLFW_RELEASE)
    {
        if (!io.KeyShift)
        {
            lynx::InjectKeyUp(key);
        }
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

    lynx::SysModule gameModule(
        "libGameExample.dll"
    );

    gameModule.RegisterFactory();


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


    std::string world_file_path_string =
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
            "Ubuntu-Regular.ttf"
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
    // Editor camera
    // ------------------------------------------------------------

    HRL_id camera =
        HRL_CreateCamera(
            scene,
            HRL_PERSPECTIVE
        );


    HRL_SetCameraPerspectiveFov(
        camera,
        20.f
    );


    HRL_id viewport =
        HRL_CreateViewport(
            scene,
            camera,
            0.f,
            0.f,
            1.f,
            1.f
        );


    // ------------------------------------------------------------
    // Debug widgets
    // ------------------------------------------------------------

    HRL_id lynx_icon_w =
        HRL_CreateWidget(
            viewport,
            HRL_WIDGET_IMAGE
        );


    auto lynx_icon_d =
        lynx::fs::ReadBinary(
            "splash/lynx-icon.png"
        );


    HRL_id lynx_icon_t =
        HRL_CreateTexture(
            reinterpret_cast<const char*>(
                lynx_icon_d.data()
            ),
            lynx_icon_d.size()
        );


    HRL_SetImageTexture(
        lynx_icon_w,
        lynx_icon_t
    );


    HRL_SetWidgetAnchor(
        lynx_icon_w,
        0.f,
        0.f
    );


    HRL_SetWidgetPosition(
        lynx_icon_w,
        0.05f,
        0.05f
    );


    HRL_SetWidgetSize(
        lynx_icon_w,
        0.14f,
        0.2f
    );


    HRL_id hrl_icon_w =
        HRL_CreateWidget(
            viewport,
            HRL_WIDGET_IMAGE
        );


    auto hrl_icon_d =
        lynx::fs::ReadBinary(
            "splash/hrl-icon.png"
        );


    HRL_id hrl_icon_t =
        HRL_CreateTexture(
            reinterpret_cast<const char*>(
                hrl_icon_d.data()
            ),
            hrl_icon_d.size()
        );


    HRL_SetImageTexture(
        hrl_icon_w,
        hrl_icon_t
    );


    HRL_SetWidgetAnchor(
        hrl_icon_w,
        1.f,
        0.f
    );


    HRL_SetWidgetPosition(
        hrl_icon_w,
        0.95f,
        0.05f
    );


    HRL_SetWidgetSize(
        hrl_icon_w,
        0.14f,
        0.2f
    );


    constexpr size_t voxelDataCount = sizeof(voxelData) / sizeof(voxelData[0]);
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


    InitImGui(win);

    lynx::SetMasterVolume(
        0.f
    );


    lynx::Audio2D ambient_cave(
        "cave_ambience.mp3"
    );

    ambient_cave.looping = true;
    ambient_cave.Play();


    // ------------------------------------------------------------
    // Brush preview
    // ------------------------------------------------------------

    auto brush_preview_circle_data =
        lynx::fs::ReadBinary(
            "brush_preview_circle.png"
        );


    HRL_id brush_preview_circle_texture =
        HRL_CreateTexture(
            reinterpret_cast<const char*>(
                brush_preview_circle_data.data()
            ),
            brush_preview_circle_data.size()
        );


    HRL_id brush_preview_widget =
        HRL_CreateWidget(
            viewport,
            HRL_WIDGET_IMAGE
        );


    HRL_SetWidgetVisible(
        brush_preview_widget,
        HRL_FALSE
    );


    HRL_SetImageTexture(
        brush_preview_widget,
        brush_preview_circle_texture
    );


    HRL_SetWidgetSize(
        brush_preview_widget,
        0.1,
        0.1
    );


    HRL_SetWidgetAnchor(
        brush_preview_widget,
        0.5,
        0.5
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


    HRL_SetCameraNearPlane(
        camera,
        0.1f
    );


    HRL_SetCameraFarPlane(
        camera,
        1000.f
    );


    HRL_SetCameraLocation(
        camera,
        camX,
        camY,
        camZ
    );


    HRL_SetCameraRotation(
        camera,
        0.f,
        -90.f,
        0.f
    );


    // ------------------------------------------------------------
    // Light
    // ------------------------------------------------------------

    HRL_id sky =
        HRL_CreateLight(
            scene,
            HRL_SKY_LIGHT
        );


    HRL_SetLightIntensity(
        sky,
        0.9f
    );


    // ------------------------------------------------------------
    // FPS
    // ------------------------------------------------------------

    double fpsTimer =
        glfwGetTime();

    int fpsFrames = 0;

    double lastFrameTime =
        glfwGetTime();


    // ------------------------------------------------------------
    // Editor settings
    // ------------------------------------------------------------

    HRL_BeginVoxelEdit(scene);


    float cameraSpeed = 50.f;

    int brushRadius = 3;

    int brushVoxelType = 4;

    float brushDensity = 1.f;

    bool brushPaintEnabled = false;

    bool brushPaintEmptyVoxels = true;


    std::unordered_set<unsigned long long>
        brushPaintedVoxels;


    bool brushPainting = false;


    struct VoxelChange
    {
        int x;
        int y;
        int previousType;
        int newType;
    };


    std::vector<
        std::vector<VoxelChange>
    > undoHistory;


    std::vector<VoxelChange>
        currentStrokeChanges;


    std::mt19937 brushRandomGenerator(
        std::random_device{}()
    );


    std::uniform_real_distribution<float>
        brushRandomDistribution(
            0.f,
            1.f
        );


    auto brushVoxelKey =
        [](int x, int y)
        -> unsigned long long
    {
        return
            (static_cast<unsigned long long>(
                static_cast<unsigned int>(x)
            ) << 32) |
            static_cast<unsigned int>(y);
    };


    auto tryPaintVoxel =
        [&](int x, int y)
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


        if (previousType == brushVoxelType)
            return;


        currentStrokeChanges.push_back({
            x,
            y,
            previousType,
            brushVoxelType
        });


        HRL_SetVoxelType(
            scene,
            x,
            y,
            brushVoxelType
        );
    };


    auto undoLastStroke =
        [&]()
    {
        if (undoHistory.empty())
            return;


        const auto& stroke =
            undoHistory.back();


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


        undoHistory.pop_back();
    };


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
        100.f
    );


    // ------------------------------------------------------------
    // Main loop
    // ------------------------------------------------------------

    bool f4_was_down = false;

    bool f3_was_down = false;

    bool ctrl_s_was_down = false;


    while (!glfwWindowShouldClose(win))
    {
        glfwPollEvents();


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
            static bool show_collision_debug = false;

            show_collision_debug =
                !show_collision_debug;


#ifdef _WIN32

            using SetCollisionDebugEnabledFn =
                void (*)(bool);


            HMODULE game_dll =
                GetModuleHandleA(
                    "libGameExample.dll"
                );


            if (game_dll)
            {
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
            }

#endif
        }


        f4_was_down = f4_down;


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
        // Mouse
        // --------------------------------------------------------

        double mouseX;
        double mouseY;


        glfwGetCursorPos(
            win,
            &mouseX,
            &mouseY
        );


        ImGuiIO& io =
            ImGui::GetIO();


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


            HRL_SaveVoxelWorldAllFile(
                scene,
                world_file_path_string.c_str()
            );

            level->SaveToFile("world.xml");
        }


        ctrl_s_was_down =
            ctrl_s;


        HRL_SetWidgetVisible(
            brush_preview_widget,
            HRL_FALSE
        );


        if (!isPlaying || io.KeyShift)
        {
            HRL_SetWidgetVisible(
                brush_preview_widget,
                HRL_TRUE
            );


            int winW;
            int winH;


            glfwGetWindowSize(
                win,
                &winW,
                &winH
            );


            HRL_SetWidgetPosition(
                brush_preview_widget,
                mouseX / winW,
                mouseY / winH
            );


            // ----------------------------------------------------
            // Gizmo
            // ----------------------------------------------------

            if (editing_actor &&
                !io.WantCaptureMouse)
            {
                dragging_object = true;


                float gx;
                float gy;
                float gz;


                HRL_GetGizmoPosition(
                    gizmo,
                    &gx,
                    &gy,
                    &gz
                );


                editing_actor->transform.location =
                {
                    gx,
                    gy,
                    gz
                };
            }


            // ----------------------------------------------------
            // Editor mouse interaction
            // ----------------------------------------------------

            if (!io.KeyCtrl &&
                !io.WantCaptureMouse)
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
                        undoHistory.push_back(
                            std::move(
                                currentStrokeChanges
                            )
                        );
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
                        for (int y = -brushRadius;
                             y <= brushRadius;
                             ++y)
                        {
                            for (int x = -brushRadius;
                                 x <= brushRadius;
                                 ++x)
                            {
                                if (x * x + y * y <=
                                    brushRadius * brushRadius)
                                {
                                    tryPaintVoxel(
                                        centerX + x,
                                        centerY + y
                                    );
                                }
                            }
                        }
                    }
                }


                // ------------------------------------------------
                // Camera movement
                // ------------------------------------------------

                if (glfwGetKey(
                        win,
                        GLFW_KEY_W
                    ) == GLFW_PRESS)
                {
                    camY +=
                        cameraSpeed * dt;
                }


                if (!ctrl_down &&
                    glfwGetKey(
                        win,
                        GLFW_KEY_S
                    ) == GLFW_PRESS)
                {
                    camY -=
                        cameraSpeed * dt;
                }


                if (glfwGetKey(
                        win,
                        GLFW_KEY_D
                    ) == GLFW_PRESS)
                {
                    camX +=
                        cameraSpeed * dt;
                }


                if (glfwGetKey(
                        win,
                        GLFW_KEY_A
                    ) == GLFW_PRESS)
                {
                    camX -=
                        cameraSpeed * dt;
                }


                HRL_SetCameraLocation(
                    camera,
                    camX,
                    camY,
                    camZ
                );


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


                    HRL_SaveVoxelWorldAllFile(
                        scene,
                        world_file_path_string.c_str()
                    );

                    level->SaveToFile("world.xml");
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
                            save_data
                        );


                    HRL_LoadVoxelWorldBuffer(
                        scene,
                        world_save_data.data(),
                        world_save_data.size()
                    );
                }
            }
            else if (io.WantCaptureMouse)
            {
                if (brushPainting)
                {
                    brushPainting = false;

                    brushPaintedVoxels.clear();


                    if (!currentStrokeChanges.empty())
                    {
                        undoHistory.push_back(
                            std::move(
                                currentStrokeChanges
                            )
                        );
                    }


                    currentStrokeChanges.clear();
                }
            }
        }


        // --------------------------------------------------------
        // F3 : Toggle Editor / Game
        // --------------------------------------------------------

        const bool f3_down =
            glfwGetKey(
                win,
                GLFW_KEY_F3
            ) == GLFW_PRESS;


        if (f3_down && !f3_was_down)
        {
            if (!isPlaying)
            {
                // ------------------------------------------------
                // EDITOR -> GAME
                // ------------------------------------------------

                HRL_SetViewportCamera(
                    viewport,
                    gameplay_cam
                );

                // Clear editor selection and hide the gizmo in game mode.
                editing_object = HRL_INVALID_ID;
                editing_actor = nullptr;

                HRL_SetGizmoVisible(
                    gizmo,
                    HRL_FALSE
                );

                isPlaying = true;


                engine->StartGame();


                std::cout
                    << "[F3] Switched to GAME mode\n";
            }
            else
            {
                // ------------------------------------------------
                // GAME -> EDITOR
                // ------------------------------------------------

                HRL_SetViewportCamera(
                    viewport,
                    camera
                );


                isPlaying = false;

				engine->EndGame();


                std::cout
                    << "[F3] Switched to EDITOR mode\n";
            }
        }


        f3_was_down =
            f3_down;


        // --------------------------------------------------------
        // Engine / HRL
        // --------------------------------------------------------

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
            100.f
        );


        HRL_SetCameraRotation(
            gameplay_cam,
            0.f,
            -90.f,
            shakenRotationZ
        );


        HRL_BeginFrame();

        HRL_EndFrame();


        // --------------------------------------------------------
        // ImGui
        // --------------------------------------------------------

        BeginImGuiFrame();


        // --------------------------------------------------------
        // Undo
        // --------------------------------------------------------

        if (io.KeyCtrl &&
            ImGui::IsKeyPressed(
                ImGuiKey_Z,
                false
            ) &&
            !brushPainting)
        {
            printf("undo\n");

            undoLastStroke();
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

                                SetActorSelected(duplicated_actor);
                            }

                            ImGui::CloseCurrentPopup();
                        }


                        if (ImGui::MenuItem("Delete"))
                        {
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

                            ImGui::CloseCurrentPopup();
                        }

                        ImGui::EndPopup();
                    }
                }
            }

            ImGui::End();


            // --------------------------------------------------------
            // Details
            // --------------------------------------------------------

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

                                case 3: // string
                                {
                                    if (auto ptr =
                                            std::get_if<std::string*>(&prop.property_member))
                                    {
                                        if (*ptr)
                                        {
                                            char buffer[1024];

                                            std::snprintf(
                                                buffer,
                                                sizeof(buffer),
                                                "%s",
                                                (*ptr)->c_str()
                                            );

                                            if (ImGui::InputText(
                                                    "##value",
                                                    buffer,
                                                    sizeof(buffer)
                                                ))
                                            {
                                                **ptr = buffer;
                                                changed = true;
                                            }
                                        }
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
                                                HRL_SetGizmoPosition(
                                                    gizmo,
                                                    value.location.x,
                                                    value.location.y,
                                                    value.location.z
                                                );
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

                            (void)changed;

                        ImGui::PopID();
                    }
                }
            }


            ImGui::End();
        }


        // --------------------------------------------------------
        // Sound
        // --------------------------------------------------------

        ImGui::Begin("Sound");


        float master_volume =
            lynx::GetMasterVolume();


        auto mv_changed =
            ImGui::SliderFloat(
                "Master Volume",
                &master_volume,
                0.f,
                2.f
            );


        if (mv_changed)
        {
            lynx::SetMasterVolume(
                master_volume
            );
        }


        ImGui::End();


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


            HRL_SetWidgetVisible(
                brush_preview_widget,
                HRL_TRUE
            );
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


            HRL_SetWidgetVisible(
                brush_preview_widget,
                HRL_FALSE
            );
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
            bool brush_radius_modified =
                ImGui::SliderInt(
                    "Brush Radius",
                    &brushRadius,
                    1,
                    20
                );


            if (brush_radius_modified)
            {
                HRL_SetWidgetSize(
                    brush_preview_widget,
                    static_cast<float>(brushRadius) / 100.f,
                    static_cast<float>(brushRadius) / 100.f
                );
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


                if (i < 3)
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
        // Render ImGui
        // --------------------------------------------------------

        EndImGuiFrame();


        glfwSwapBuffers(win);
    }


    // ------------------------------------------------------------
    // Shutdown
    // ------------------------------------------------------------

    delete lynx::GetEngine();

    ShutdownImGui();


    return 0;
}