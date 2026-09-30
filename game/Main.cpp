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

#include "Common.h"
#include "core/Private/SystemModule.h"
#include "gameplay/Private/InputManager.h"


float camX;
float camY;
float camZ = 200.f;
bool dragging_object=false;
bool isPlaying=false;

static void ErrorCallback(HRL_EError code, HRL_ESeverity severity, const char* detail)
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

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
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
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}


void ShutdownImGui()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}


static void FramebufferSizeCallback(GLFWwindow*, int width, int height)
{
    HRL_WindowResizeCallback(width, height);
}

static double lastMouseX = 0.0;
static double lastMouseY = 0.0;
static bool haveLastMousePosition = false;

static void MouseMove(GLFWwindow*, double x, double y)
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
lynx::Actor* editing_actor=nullptr;


void SetObjectSelected(HRL_id object)
{
    if (object == HRL_INVALID_ID)
    {
        editing_actor=nullptr;
        HRL_SetGizmoVisible(gizmo, HRL_FALSE);
    }
    else
    {
        editing_actor = (lynx::Actor*)HRL_GetMeshUserHandle(object);
        if (!editing_actor) printf("nullptr");
        HRL_SetGizmoPosition(gizmo, editing_actor->transform.location.x, editing_actor->transform.location.y, editing_actor->transform.location.z);
        HRL_SetGizmoVisible(gizmo, HRL_TRUE);
    }
}

static void MouseButtonCallback(
    GLFWwindow* window,
    int button,
    int action,
    int mods)
{
    if (action == GLFW_PRESS)
        lynx::InjectMouseButtonDown(button);
    else if (action == GLFW_RELEASE)
        lynx::InjectMouseButtonUp(button);

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
            if (io.KeyCtrl)
            {
                HRL_id object = HRL_GL_GetHoveredObject(scene, (int)mouseX, (int)mouseY, nullptr);
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

    // Clic gauche = HRL / gizmo / UI
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

void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    lynx::InjectMouseWheel(static_cast<float>(yoffset));
    camZ -= (float)yoffset*10;
}


void inject_keys_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
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
    lynx::Engine* engine = lynx::CreateEngine("", false);

    lynx::LoadConfigFile("input.json");

    lynx::SysModule gameModule("libGameExample.dll");
    gameModule.RegisterFactory();


    glfwInit();

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(
        1280,
        720,
        "Lynx ImGui Editor",
        nullptr,
        nullptr
    );

    glfwMakeContextCurrent(win);

    glfwSetFramebufferSizeCallback(win, FramebufferSizeCallback);
    glfwSetMouseButtonCallback(win, MouseButtonCallback);
    glfwSetCursorPosCallback(win, MouseMove);
    glfwSetScrollCallback(win, ScrollCallback);
    glfwSetKeyCallback(win, inject_keys_callback);


    // Désactiver la V-Sync
    glfwSwapInterval(0);

    HRL_Init(HRL_OPENGL_33);
    HRL_InitContext(
        1280,
        720,
        (void*)glfwGetProcAddress
    );

    HRL_RegisterErrorCallback(ErrorCallback);
    HRL_SetDebugLineThickness(3.f);



    scene = HRL_CreateScene(true);
    lynx::SetSceneID(scene);


    lynx::CameraShake cameraShake;
    lynx::SetCameraShake(cameraShake);


    HRL_id gameplay_cam = HRL_CreateCamera(scene, HRL_PERSPECTIVE);
    HRL_SetCameraPerspectiveFov(gameplay_cam, 20.f);
    HRL_SetCameraRotation(
        gameplay_cam,
        0.f,
        -90.f,
        0.f
    );


    //load world
    auto save_file = lynx::fs::ReadBinary("save_file.txt");
    std::string save_path(
        reinterpret_cast<const char*>(save_file.data()),
        save_file.size()
    );

    // Retirer le \n éventuel
    while (!save_path.empty() &&
           (save_path.back() == '\n' || save_path.back() == '\r'))
    {
        save_path.pop_back();
    }

    if (lynx::fs::Exists(save_path))
    {
        auto world_file = lynx::fs::ReadBinary(save_path);

        HRL_LoadVoxelWorldBuffer(
            scene,
            world_file.data(),
            world_file.size()
        );
    }
    else
    {
        HRL_SetVoxelSize(scene, 4096, 4096);
        HRL_SetVoxelPhysicalSize(scene, 0.2f);
        HRL_SetVoxelChunkSize(scene, 16);

        HRL_SaveVoxelWorldAllFile(
            scene,
            save_path.c_str()
        );
    }

    engine->CreateLevel("world.xml");


    //

    HRL_id camera = HRL_CreateCamera(
        scene,
        HRL_PERSPECTIVE
    );

    HRL_SetCameraPerspectiveFov(
        camera,
        20.f
    );

    HRL_id viewport = HRL_CreateViewport(
        scene,
        camera,
        0.f,
        0.f,
        1.f,
        1.f
    );

    for (int i = 0; i < 4; ++i)
    {
        int type = i + 1;

        HRL_SetVoxelTypeColor(
            scene,
            type,
            voxelColors[i][0],
            voxelColors[i][1],
            voxelColors[i][2],
            voxelColors[i][3]
        );

        HRL_SetVoxelTypeCollisionFlags(
            scene,
                type,
                HRL_VOXEL_COLLISION_LEFT |
                HRL_VOXEL_COLLISION_RIGHT |
                HRL_VOXEL_COLLISION_TOP |
                HRL_VOXEL_COLLISION_BOTTOM
        );
    }

    HRL_SetVoxelTypeEmissiveColor(
        scene,
        4,
        2.f,
        2.f,
        2.f
    );



    HRL_id post_mat = HRL_CreateMaterial(HRL_DEFAULT_POST_PROCESS_SHADER);
    HRL_id post = HRL_CreatePostProcess(viewport, post_mat, 1);
    HRL_MaterialSetFloat(post_mat, "vignetteStrength", 0.4f);


    InitImGui(win);



    // ------------------------------------------------------------
    // Load ressources
    // ------------------------------------------------------------

    auto brush_preview_circle_data = lynx::fs::ReadBinary(
        "brush_preview_circle.png"
    );

    HRL_id brush_preview_circle_texture = HRL_CreateTexture(
        reinterpret_cast<const char*>(brush_preview_circle_data.data()),
        brush_preview_circle_data.size()
    );


    HRL_id brush_preview_widget = HRL_CreateWidget(viewport, HRL_WIDGET_IMAGE);
    HRL_SetWidgetVisible(brush_preview_widget, HRL_FALSE);
    HRL_SetImageTexture(brush_preview_widget, brush_preview_circle_texture);
    HRL_SetWidgetSize(brush_preview_widget, 0.1, 0.1);
    HRL_SetWidgetAnchor(brush_preview_widget, 0.5, 0.5);



    gizmo = HRL_CreateGizmo(viewport);
    HRL_SetGizmoVisible(gizmo, HRL_FALSE);


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
        HRL_GIZMO_AXIS_X | HRL_GIZMO_AXIS_Y | HRL_GIZMO_AXIS_Z
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

    HRL_id sky = HRL_CreateLight(
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

    double fpsTimer = glfwGetTime();
    int fpsFrames = 0;

    // Vrai delta time entre deux frames
    double lastFrameTime = glfwGetTime();


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

    // Each voxel is considered only once during a brush stroke.
    // This prevents the random density test from being rerolled
    // every frame when the mouse is held over the same voxel.
    std::unordered_set<unsigned long long> brushPaintedVoxels;
    bool brushPainting = false;

    // One entry per voxel changed by the current stroke.
    struct VoxelChange
    {
        int x;
        int y;
        int previousType;
        int newType;
    };

    // Each stroke is one undo step.
    std::vector<std::vector<VoxelChange>> undoHistory;
    std::vector<VoxelChange> currentStrokeChanges;

    std::mt19937 brushRandomGenerator(std::random_device{}());
    std::uniform_real_distribution<float> brushRandomDistribution(0.f, 1.f);

    auto brushVoxelKey = [](int x, int y) -> unsigned long long
    {
        return (static_cast<unsigned long long>(
                    static_cast<unsigned int>(x)
                ) << 32) |
               static_cast<unsigned int>(y);
    };

    auto tryPaintVoxel = [&](int x, int y)
    {
        const unsigned long long key = brushVoxelKey(x, y);

        // Never reroll/repaint a voxel that was already considered
        // during this continuous brush stroke.
        if (!brushPaintedVoxels.insert(key).second)
            return;

        // Density is a probability:
        // 1.0 = every voxel, 0.5 = roughly half, 0.0 = none.
        if (brushDensity <= 0.f)
            return;

        if (brushDensity < 1.f &&
            brushRandomDistribution(brushRandomGenerator) >= brushDensity)
        {
            return;
        }

        const int previousType = HRL_GetVoxelType(scene, x, y);

        // Do not paint empty voxels when disabled.
        if (!brushPaintEmptyVoxels && previousType == 0)
            return;

        // Avoid creating useless undo entries.
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

    auto undoLastStroke = [&]()
    {
        if (undoHistory.empty())
            return;

        const auto& stroke = undoHistory.back();

        // Restore in reverse order.
        for (auto it = stroke.rbegin(); it != stroke.rend(); ++it)
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


    auto* player = lynx::GetEngine()->GetCurrentLevel()->GetActorFromID("Pawn");

    gameplayCamX = player->transform.location.x;
    gameplayCamY = player->transform.location.y;

    HRL_SetCameraLocation(
        gameplay_cam,
        gameplayCamX,
        gameplayCamY,
        100.f
    );


    // ------------------------------------------------------------
    // Main loop
    // ------------------------------------------------------------

    while (!glfwWindowShouldClose(win))
    {
        glfwPollEvents();

        // --------------------------------------------------------
        // Delta time
        // --------------------------------------------------------

        double currentFrameTime = glfwGetTime();

        float dt = static_cast<float>(
            currentFrameTime - lastFrameTime
        );

        lastFrameTime = currentFrameTime;

        // Évite un déplacement énorme après un freeze,
        // une minimisation de fenêtre, etc.
        dt = std::min(dt, 0.1f);


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

        double now = glfwGetTime();

        if (now - fpsTimer >= 0.25)
        {
            double fps =
                static_cast<double>(fpsFrames) /
                (now - fpsTimer);

            char title[128];

            std::snprintf(
                title,
                sizeof(title),
                "Lynx ImGui Editor - %.0f FPS",
                fps
            );

            glfwSetWindowTitle(
                win,
                title
            );

            fpsFrames = 0;
            fpsTimer = now;
        }


        // Le suivi et le shake de la caméra sont appliqués après
        // ProgressOneFrame(), afin que les events d'animation
        // (dont l'event de frame 4 de l'attaque) aient le temps
        // d'appeler lynx::GetCameraShake().Trigger() avant lynx::GetCameraShake().Update().

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

        auto io = ImGui::GetIO();

        if (!isPlaying)
        {
            int winW, winH;
            glfwGetWindowSize(win, &winW, &winH);
            HRL_SetWidgetPosition(brush_preview_widget, mouseX/winW, mouseY/winH);




            //Gizmo
            if (editing_object != HRL_INVALID_ID && editing_actor)
            {
                dragging_object=true;
                float gx, gy, gz;
                HRL_GetGizmoPosition(gizmo, &gx, &gy, &gz);
                editing_actor->transform.location = {gx, gy, gz};
            }



            if (!io.KeyCtrl)
            {
                // --------------------------------------------------------
                // Brush
                // --------------------------------------------------------

                const bool leftMousePressed =
                    glfwGetMouseButton(
                        win,
                        GLFW_MOUSE_BUTTON_LEFT
                    ) == GLFW_PRESS;

                // Start a new brush stroke only when painting is enabled.
                if (brushPaintEnabled &&
                    leftMousePressed &&
                    !brushPainting)
                {
                    brushPainting = true;
                    brushPaintedVoxels.clear();
                    currentStrokeChanges.clear();
                }

                // End the current brush stroke.
                if ((!leftMousePressed || !brushPaintEnabled) && brushPainting)
                {
                    brushPainting = false;
                    brushPaintedVoxels.clear();

                    if (!currentStrokeChanges.empty())
                        undoHistory.push_back(std::move(currentStrokeChanges));

                    currentStrokeChanges.clear();
                }

                if (brushPaintEnabled && leftMousePressed)
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
                        // Circular brush. Each voxel is passed through
                        // tryPaintVoxel only once per continuous stroke.
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


                // --------------------------------------------------------
                // Camera movement
                // --------------------------------------------------------

                if (glfwGetKey(
                        win,
                        GLFW_KEY_W
                    ) == GLFW_PRESS)
                {
                    camY += cameraSpeed * dt;
                }

                if (glfwGetKey(
                        win,
                        GLFW_KEY_S
                    ) == GLFW_PRESS)
                {
                    camY -= cameraSpeed * dt;
                }

                if (glfwGetKey(
                        win,
                        GLFW_KEY_D
                    ) == GLFW_PRESS)
                {
                    camX += cameraSpeed * dt;
                }

                if (glfwGetKey(
                        win,
                        GLFW_KEY_A
                    ) == GLFW_PRESS)
                {
                    camX -= cameraSpeed * dt;
                }

                HRL_SetCameraLocation(
                    camera,
                    camX,
                    camY,
                    camZ
                );


                // --------------------------------------------------------
                // Save / Load
                // --------------------------------------------------------

                if (glfwGetKey(
                        win,
                        GLFW_KEY_F1
                    ) == GLFW_PRESS)
                {
                    std::filesystem::path p = std::filesystem::path("assets/") / save_path;
                    std::string str = p.string();
                    HRL_SaveVoxelWorldAllFile(
                        scene,
                        str.c_str()
                    );
                }

                if (glfwGetKey(
                        win,
                        GLFW_KEY_F2
                    ) == GLFW_PRESS)
                {
                    auto world_save_data =
                        lynx::fs::ReadBinary("save.hrlv");

                    HRL_LoadVoxelWorldBuffer(
                        scene,
                        world_save_data.data(),
                        world_save_data.size()
                    );
                }
            }
        }

        if (glfwGetKey( win, GLFW_KEY_F3 ) == GLFW_PRESS)
        {
            HRL_SetViewportCamera(viewport, gameplay_cam);
            isPlaying=true;
            engine->StartGame();
        }




        // --------------------------------------------------------
        // Engine / HRL
        // --------------------------------------------------------


        engine->ProgressOneFrame(dt);

        // --------------------------------------------------------
        // Gameplay camera
        // --------------------------------------------------------

        float targetX = player->transform.location.x;
        float targetY = player->transform.location.y;

        // Vitesse de suivi de la caméra.
        // Plus c'est petit, plus la caméra a du retard.
        float cameraFollowSpeed = 3.f;

        float follow = 1.f - std::exp(-cameraFollowSpeed * dt);

        gameplayCamX += (targetX - gameplayCamX) * follow;
        gameplayCamY += (targetY - gameplayCamY) * follow;

        float shakenX = gameplayCamX;
        float shakenY = gameplayCamY;
        float shakenRotationZ = 0.f;

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
            ImGui::IsKeyPressed(ImGuiKey_Z, false) &&
            !brushPainting)
        {
            printf("undo\n");
            undoLastStroke();
        }


        // --------------------------------------------------------
        // Viewport
        // --------------------------------------------------------

        ImGui::Begin("Color Picking");

        ImVec2 viewportSize =
            ImGui::GetContentRegionAvail();

        ImGui::Image(
            HRL_GL_GetSceneColorPickingBufferGL_ID(scene),
            viewportSize,
            ImVec2(0, 1),
            ImVec2(1, 0)
        );

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

        ImGui::Text("Brush Mode");

        if (ImGui::Button("Paint"))
        {
            brushPaintEnabled = true;
            HRL_SetWidgetVisible(brush_preview_widget, HRL_TRUE);
        }

        if (brushPaintEnabled)
        {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();

            ImGui::GetWindowDrawList()->AddRect(
                ImVec2(min.x - 2.f, min.y - 2.f),
                ImVec2(max.x + 2.f, max.y + 2.f),
                IM_COL32(255, 255, 255, 255),
                2.f,
                0,
                2.f
            );
        }

        ImGui::SameLine();

        if (ImGui::Button("No Painting"))
        {
            brushPaintEnabled = false;
            HRL_SetWidgetVisible(brush_preview_widget, HRL_FALSE);
        }

        if (!brushPaintEnabled)
        {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();

            ImGui::GetWindowDrawList()->AddRect(
                ImVec2(min.x - 2.f, min.y - 2.f),
                ImVec2(max.x + 2.f, max.y + 2.f),
                IM_COL32(255, 255, 255, 255),
                2.f,
                0,
                2.f
            );
        }


        if (brushPaintEnabled)
        {
            bool brush_radius_modified = ImGui::SliderInt(
                "Brush Radius",
                &brushRadius,
                1,
                20
            );

            if (brush_radius_modified)
            {
                //HRL_SetMeshScale(brush_preview_mesh, (float)brushRadius, (float)brushRadius, 1);
                HRL_SetWidgetSize(brush_preview_widget, (float)brushRadius/100, (float)brushRadius/100);
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


        ImGui::Text("Voxel Type");

        // Type 0 = empty / erase.
        ImGui::PushID(0);
        if (ImGui::ColorButton(
                "##VoxelEmpty",
                ImVec4(0.f, 0.f, 0.f, 1.f),
                ImGuiColorEditFlags_NoTooltip,
                ImVec2(40.f, 40.f)
            ))
        {
            brushVoxelType = 0;
        }

        if (brushVoxelType == 0)
        {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();

            ImGui::GetWindowDrawList()->AddRect(
                ImVec2(min.x - 2.f, min.y - 2.f),
                ImVec2(max.x + 2.f, max.y + 2.f),
                IM_COL32(255, 255, 255, 255),
                2.f,
                0,
                2.f
            );
        }
        ImGui::SameLine();
        ImGui::PopID();

        for (int i = 0; i < 4; ++i)
        {
            int type = i + 1;

            ImVec4 color(
                voxelColors[i][0],
                voxelColors[i][1],
                voxelColors[i][2],
                voxelColors[i][3]
            );

            ImGui::PushID(type);

            if (ImGui::ColorButton(
                "##VoxelColor",
                color,
                ImGuiColorEditFlags_NoTooltip,
                ImVec2(40.f, 40.f)
            ))
            {
                brushVoxelType = type;
            }

            if (brushVoxelType == type)
            {
                ImVec2 min = ImGui::GetItemRectMin();
                ImVec2 max = ImGui::GetItemRectMax();

                ImGui::GetWindowDrawList()->AddRect(
                    ImVec2(min.x - 2.f, min.y - 2.f),
                    ImVec2(max.x + 2.f, max.y + 2.f),
                    IM_COL32(255, 255, 255, 255),
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

        ImGui::Begin("Camera Shake");

    ImGui::Separator();
    ImGui::Text("Camera Shake");

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

        if (ImGui::Button("Test Camera Shake"))
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