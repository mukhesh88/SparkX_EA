#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#if defined(HAS_GLAD)
#include <glad/glad.h>
#elif defined(_WIN32)
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <GLFW/glfw3.h>
#include <iostream>

#include "app_state.hpp"
#include "theme.hpp"
#include "onnx_laya.hpp"
#include "zmq_listener.hpp"
#include "ui_panels.hpp"

static void glfw_error_callback(int error, const char* description) {
    std::cerr << "[GLFW Error " << error << "]: " << description << std::endl;
}

int main(int argc, char** argv) {
    std::cout << "================================================================================" << std::endl;
    std::cout << "  SPARKX TERMINAL // ULTRA-LOW-LATENCY SMC DECISION ENGINE (LAYA-ONNX C++)" << std::endl;
    std::cout << "================================================================================" << std::endl;

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::cerr << "[FATAL] Failed to initialize GLFW." << std::endl;
        return 1;
    }

    // OpenGL 3.3 Core Profile configuration
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    // Create GLFW window with dark titlebar
    GLFWwindow* window = glfwCreateWindow(1500, 920, "SparkX Terminal // Laya SMC Algorithmic Engine", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "[FATAL] Failed to create GLFW window." << std::endl;
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable VSync

#if defined(HAS_GLAD)
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "[FATAL] Failed to initialize GLAD OpenGL loader." << std::endl;
        return 1;
    }
#endif

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Load TrueType JetBrains Mono fonts at multiple sizes (14px, 17px, 24px, 12px)
    CyberpunkTheme::LoadFonts(io);

    // Apply Brand Cyberpunk Styling (#0A0A0C, #121215, #1E1E24, #FF003C)
    CyberpunkTheme::ApplyTheme();

    // Initialize ImGui Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // 1. Initialize State & Logging
    ThreadSafeAppState app_state;
    app_state.AddLog("INFO", "SparkX Terminal Engine core initialized.");

    // 2. Initialize Laya ONNX Engine (NVIDIA RTX 4060 CUDA Execution Provider)
    LayaONNXEngine onnx_engine("models/laya.onnx", 0);
    onnx_engine.Initialize();
    app_state.AddLog("INFO", "Laya ONNX Engine online. Provider: " + onnx_engine.GetProviderName());

    // 3. Launch Background Data Stream Worker Thread
    std::string zmq_endpoint = "127.0.0.1:5556";
    if (argc > 1) {
        zmq_endpoint = argv[1];
    }
    ZMQListenerWorker zmq_worker(app_state, onnx_engine, zmq_endpoint);
    zmq_worker.Start();

    // Main GLFW / ImGui Render Loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Start new ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Create Fullscreen Borderless Docking Surface
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar |
                                       ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus |
                                       ImGuiWindowFlags_NoNavFocus |
                                       ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoScrollWithMouse;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));

        ImGui::Begin("SparkXMasterViewport", nullptr, window_flags);
        ImGui::PopStyleVar(3);

        // Render Dashboard Panels (Header, Primitives, SMC Context, Console)
        UIPanels::RenderDashboard(app_state);

        ImGui::End();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(
            CyberpunkTheme::BG_DEEP_DARK.x,
            CyberpunkTheme::BG_DEEP_DARK.y,
            CyberpunkTheme::BG_DEEP_DARK.z,
            CyberpunkTheme::BG_DEEP_DARK.w
        );
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Graceful Shutdown
    std::cout << "[SparkX] Initiating graceful shutdown..." << std::endl;
    zmq_worker.Stop();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "[SparkX] Terminated successfully." << std::endl;
    return 0;
}
