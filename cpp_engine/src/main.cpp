#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#if defined(HAS_GLAD)
#include <glad/glad.h>
#elif defined(_WIN32)
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <GLFW/glfw3.h>
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

#include "app_state.hpp"
#include "theme.hpp"
#include "onnx_laya.hpp"
#include "zmq_listener.hpp"
#include "ui_panels.hpp"

struct BackendHandle {
#if defined(_WIN32)
    PROCESS_INFORMATION pi{};
#endif
    bool started = false;
};

#if defined(_WIN32)
static bool IsBackendServerRunning(int port = 5556) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    DWORD timeout = 250;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

    bool connected = (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0);
    closesocket(s);
    WSACleanup();
    return connected;
}
#else
static bool IsBackendServerRunning(int port = 5556) {
    (void)port;
    return false;
}
#endif

static std::string FindWorkspaceRoot() {
    namespace fs = std::filesystem;
    try {
        if (fs::exists("python_node/publisher.py")) {
            return fs::current_path().string();
        }
#if defined(_WIN32)
        char exe_path[MAX_PATH];
        if (GetModuleFileNameA(NULL, exe_path, MAX_PATH) > 0) {
            fs::path p(exe_path);
            fs::path dir = p.parent_path();
            for (int i = 0; i < 5; ++i) {
                if (fs::exists(dir / "python_node" / "publisher.py")) {
                    return dir.string();
                }
                if (!dir.has_parent_path()) break;
                dir = dir.parent_path();
            }
        }
#endif
    } catch (...) {}
    return ".";
}

static BackendHandle LaunchPythonBackend(const std::string& workspace_dir) {
    BackendHandle handle;
#if defined(_WIN32)
    namespace fs = std::filesystem;
    fs::path ws(workspace_dir);

    // 1. Check for standalone compiled backend binary
    std::vector<fs::path> backend_candidates = {
        ws / "dist" / "SparkX_Backend" / "SparkX_Backend.exe",
        ws / "SparkX_Backend.exe",
        ws / "build" / "SparkX_Backend.exe",
        ws / "SparkX_Backend" / "SparkX_Backend.exe"
    };

    std::string cmd;
    for (const auto& candidate : backend_candidates) {
        if (fs::exists(candidate)) {
            cmd = "\"" + candidate.string() + "\" --interval 0.5";
            std::cout << "[BackendManager] Found standalone backend binary: " << candidate.string() << std::endl;
            break;
        }
    }

    if (cmd.empty()) {
        fs::path venv_python = ws / ".venv" / "Scripts" / "python.exe";
        std::string python_bin = fs::exists(venv_python) ? venv_python.string() : "python.exe";
        fs::path script = ws / "python_node" / "publisher.py";
        cmd = "\"" + python_bin + "\" \"" + script.string() + "\" --interval 0.5";
    }

    std::cout << "[BackendManager] Spawning backend: " << cmd << " (CWD: " << workspace_dir << ")" << std::endl;

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // Run background service hidden without popup console

    std::vector<char> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back('\0');

    BOOL ok = CreateProcessA(
        NULL,
        cmd_buf.data(),
        NULL,
        NULL,
        FALSE,
        CREATE_NO_WINDOW,
        NULL,
        workspace_dir.c_str(),
        &si,
        &handle.pi
    );

    if (ok) {
        handle.started = true;
        std::cout << "[BackendManager] Python MT5 Trading Publisher started (PID: " << handle.pi.dwProcessId << ")" << std::endl;
    } else {
        std::cerr << "[BackendManager] Failed to launch Python backend (Error: " << GetLastError() << ")" << std::endl;
    }
#endif
    return handle;
}

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
    io.ConfigDebugHighlightIdConflicts = false; // Disable debug programmer modal popups in production runtime

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

    // Auto-launch Python backend if not already running
    std::string ws_dir = FindWorkspaceRoot();
    BackendHandle backend_proc;
    if (!IsBackendServerRunning(5556)) {
        app_state.AddLog("INFO", "Auto-starting Python MT5 Trading Backend...");
        backend_proc = LaunchPythonBackend(ws_dir);
        if (backend_proc.started) {
#if defined(_WIN32)
            app_state.AddLog("INFO", "Backend spawned (PID: " + std::to_string(backend_proc.pi.dwProcessId) + ")");
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
        }
    } else {
        app_state.AddLog("INFO", "Live Python MT5 backend detected on port 5556.");
    }

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

#if defined(_WIN32)
    if (backend_proc.started && backend_proc.pi.hProcess) {
        std::cout << "[BackendManager] Terminating child backend process (PID: " 
                  << backend_proc.pi.dwProcessId << ")..." << std::endl;
        TerminateProcess(backend_proc.pi.hProcess, 0);
        CloseHandle(backend_proc.pi.hProcess);
        CloseHandle(backend_proc.pi.hThread);
    }
#endif

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "[SparkX] Terminated successfully." << std::endl;
    return 0;
}
