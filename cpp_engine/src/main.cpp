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

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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
    std::cout << "  SPARKX EA // ULTRA-LOW-LATENCY SMC DECISION ENGINE (LAYA-ONNX C++)" << std::endl;
    std::cout << "================================================================================" << std::endl;

    bool screenshot_mode = false;
    std::string screenshot_file = "";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--screenshot" && i + 1 < argc) {
            screenshot_mode = true;
            screenshot_file = argv[++i];
        }
    }

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
    if (screenshot_mode) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    // Create GLFW window with dark titlebar
    GLFWwindow* window = glfwCreateWindow(1500, 920, "SparkX EA // Laya SMC Algorithmic Engine", nullptr, nullptr);
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
    app_state.AddLog("INFO", "SparkX EA Engine core initialized.");

    if (screenshot_mode) {
        MarketFrame frame;
        frame.symbol = "XAUUSD";
        frame.price = 2685.40;
        frame.bid = 2685.20;
        frame.ask = 2685.60;
        frame.spread = 0.40f;
        frame.session = "NY_OVERLAP";
        frame.feed_source = "MT5_LIVE";
        frame.is_live_feed = true;
        frame.is_fresh = true;
        frame.token_count = 142;
        frame.ingest_latency_ms = 1.25f;

        frame.bias.h1_trend = "BULLISH";
        frame.bias.m15_struct = "BOS_BULLISH";
        frame.bias.m5_struct = "MSS_BULLISH";
        frame.bias.zone = "DISCOUNT (Fib: 61.8%)";
        frame.bias.fib_pct = 61.8f;
        frame.liquidity.asia_high = 2678.50;
        frame.liquidity.asia_low = 2662.10;
        frame.liquidity.ssl_swept = true;
        frame.liquidity.bsl_target = 2698.00;
        frame.liquidity.ssl_target = 2658.00;
        frame.arrays.fvg_active = true;
        frame.arrays.fvg_direction = "BULLISH_FVG";
        frame.arrays.ob_active = true;
        frame.arrays.ob_direction = "BULLISH_OB";

        frame.laya_decision.choice_action = "MARKET_BUY";
        frame.laya_decision.choice_confidence = 0.942f;
        frame.laya_decision.choice_distribution = {
            {"BUY", 0.68f},
            {"HOLD", 0.18f},
            {"LIM_B", 0.10f},
            {"SELL", 0.04f}
        };
        frame.laya_decision.score_grade = 8.8f;
        frame.laya_decision.score_top_grade = 9;
        frame.laya_decision.inference_latency_ms = 3.85f;
        frame.laya_decision.gpu_accelerated = true;
        frame.laya_decision.execution_provider = "CUDA_RTX4060";
        frame.laya_decision.noul_checks = {
            {"H1 Trend Alignment", 0.96f, 0.04f, true},
            {"Asian Low Swept & Reclaimed", 0.91f, 0.09f, true},
            {"Fresh M5 FVG Confirmed", 0.88f, 0.12f, true},
            {"MSS with Volume Displacement", 0.85f, 0.15f, true}
        };

        app_state.UpdateMarketFrame(frame);

        TradePosition pos;
        pos.ticket = 58749506432;
        pos.time_str = "13:42:10";
        pos.symbol = "XAUUSD";
        pos.type = "BUY";
        pos.volume = 0.10;
        pos.price_open = 2685.40;
        pos.price_current = 2692.60;
        pos.sl = 2680.90;
        pos.tp = 2697.40;
        pos.profit = 720.00;
        pos.comment = "SparkX_Laya_S9";
        app_state.UpdatePositions({pos});

        std::vector<TradeHistoryItem> hist = {
            TradeHistoryItem{58748921104, 0, "12:15:30", "XAUUSD", "BUY", 0.10, 2674.20, 2684.50, 1030.00, "WIN", "TP Hit (+10.3pts)"},
            TradeHistoryItem{58748234912, 0, "10:30:15", "XAUUSD", "SELL", 0.10, 2680.10, 2675.20, 490.00, "WIN", "TP Hit (+4.9pts)"},
            TradeHistoryItem{58747651239, 0, "08:45:00", "XAUUSD", "BUY", 0.10, 2671.00, 2666.50, -450.00, "LOSS", "SL Hit (-4.5pts)"},
            TradeHistoryItem{58746981240, 0, "06:20:10", "XAUUSD", "BUY", 0.10, 2663.50, 2672.00, 850.00, "WIN", "TP Hit (+8.5pts)"}
        };
        app_state.UpdateHistory(hist);

        app_state.AddLog("INFO", "Laya ONNX Engine online. Provider: CUDAExecutionProvider (NVIDIA GeForce RTX 4060).");
        app_state.AddLog("INFO", "Connected to Live MT5 Stream on 127.0.0.1:5556.");
        app_state.AddLog("ALERT", "SMC Confluence: ASIAN_LOW_SWEPT + M5 Bullish FVG Mitigated.");
        app_state.AddLog("EXEC", "Laya Decision: MARKET_BUY | Confidence: 94.2% | Score: 8.8/10.0.");
        app_state.AddLog("EXEC", "MT5 Order Executed: BUY 0.10 XAUUSD @ $2685.40 | Ticket #58749506432.");

        // Render 10 frames to let fonts, sparklines, and auto-sizing settle
        for (int frame_idx = 0; frame_idx < 10; ++frame_idx) {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));

            ImGui::Begin("SparkXMasterViewport", nullptr, flags);
            ImGui::PopStyleVar(3);

            UIPanels::RenderDashboard(app_state);

            ImGui::End();

            ImGui::Render();
            int dw, dh;
            glfwGetFramebufferSize(window, &dw, &dh);
            glViewport(0, 0, dw, dh);
            glClearColor(0.04f, 0.04f, 0.05f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }

        // Capture Framebuffer
        int dw, dh;
        glfwGetFramebufferSize(window, &dw, &dh);
        std::vector<unsigned char> pixels(dw * dh * 4);
        glReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        stbi_flip_vertically_on_write(1);
        int ok = stbi_write_png(screenshot_file.c_str(), dw, dh, 4, pixels.data(), dw * 4);
        if (ok) {
            std::cout << "[Screenshot] Successfully captured actual UI to " << screenshot_file << " (" << dw << "x" << dh << ")" << std::endl;
        } else {
            std::cerr << "[Screenshot] Failed to write image to " << screenshot_file << std::endl;
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return ok ? 0 : 1;
    }

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
