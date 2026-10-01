#include "theme.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <cstdio>

namespace CyberpunkTheme {

ImFont* g_font_regular = nullptr;
ImFont* g_font_bold_large = nullptr;
ImFont* g_font_bold_med = nullptr;
ImFont* g_font_small = nullptr;

void LoadFonts(ImGuiIO& io) {
    std::vector<std::string> search_dirs = {
        "assets/fonts/",
        "../assets/fonts/",
        "../../assets/fonts/",
        "C:/Users/M S I/Documents/Spark_Ai_Algo/assets/fonts/",
        "C:/Users/MSI~1/Documents/Spark_Ai_Algo/assets/fonts/"
    };

    // 1. Try loading Lemon Milk font family
    std::string lemon_reg, lemon_bold, lemon_med;
    for (const auto& dir : search_dirs) {
        std::string r = dir + "LEMONMILK-Regular.otf";
        std::string b = dir + "LEMONMILK-Bold.otf";
        std::string m = dir + "LEMONMILK-Medium.otf";
        FILE* f = fopen(r.c_str(), "rb");
        if (f) {
            fclose(f);
            lemon_reg = r;
            lemon_bold = b;
            lemon_med = m;
            break;
        }
    }

    if (!lemon_reg.empty()) {
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;
        cfg.RasterizerMultiply = 1.05f;

        g_font_regular = io.Fonts->AddFontFromFileTTF(lemon_reg.c_str(), 13.5f, &cfg);
        g_font_bold_large = io.Fonts->AddFontFromFileTTF(lemon_bold.c_str(), 23.0f, &cfg);
        g_font_bold_med = io.Fonts->AddFontFromFileTTF(!lemon_med.empty() ? lemon_med.c_str() : lemon_bold.c_str(), 16.0f, &cfg);
        g_font_small = io.Fonts->AddFontFromFileTTF(lemon_reg.c_str(), 11.5f, &cfg);

        if (g_font_regular && g_font_bold_large) {
            io.FontDefault = g_font_regular;
            std::cout << "[Theme] Lemon Milk font family loaded successfully from: " << lemon_reg << std::endl;
            return;
        }
    }

    // 2. Fallback to JetBrains Mono if Lemon Milk fails
    std::string reg_path, bold_path;
    for (const auto& dir : search_dirs) {
        std::string r = dir + "JetBrainsMono-Regular.ttf";
        std::string b = dir + "JetBrainsMono-Bold.ttf";
        FILE* f = fopen(r.c_str(), "rb");
        if (f) {
            fclose(f);
            reg_path = r;
            bold_path = b;
            break;
        }
    }

    if (!reg_path.empty()) {
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;
        cfg.RasterizerMultiply = 1.05f;

        g_font_regular = io.Fonts->AddFontFromFileTTF(reg_path.c_str(), 14.0f, &cfg);
        g_font_bold_large = io.Fonts->AddFontFromFileTTF(bold_path.c_str(), 24.0f, &cfg);
        g_font_bold_med = io.Fonts->AddFontFromFileTTF(bold_path.c_str(), 16.5f, &cfg);
        g_font_small = io.Fonts->AddFontFromFileTTF(reg_path.c_str(), 12.0f, &cfg);
        io.FontDefault = g_font_regular;
        std::cout << "[Theme] JetBrains Mono TTF loaded as fallback: " << reg_path << std::endl;
    } else {
        std::cout << "[Theme] Fallback: using default embedded ImGui font." << std::endl;
        g_font_regular = io.Fonts->AddFontDefault();
        g_font_bold_large = g_font_regular;
        g_font_bold_med = g_font_regular;
        g_font_small = g_font_regular;
    }
}

void ApplyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Window & Backgrounds - Sleek Modern Dark Glassmorphism
    // ImGuiCol_WindowBg -> #0A0A0C
    colors[ImGuiCol_WindowBg]             = BG_WINDOW;
    // ImGuiCol_ChildBg  -> #121215
    colors[ImGuiCol_ChildBg]              = BG_PANEL;
    colors[ImGuiCol_PopupBg]              = ImVec4(0.063f, 0.063f, 0.075f, 0.98f);
    
    // ImGuiCol_Border   -> #1E1E24
    colors[ImGuiCol_Border]               = BORDER_DARK;
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);

    // Text & Headers
    colors[ImGuiCol_Text]                 = TEXT_PRIMARY;
    colors[ImGuiCol_TextDisabled]         = TEXT_MUTED;
    colors[ImGuiCol_Header]               = ImVec4(0.14f, 0.14f, 0.18f, 0.70f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.24f, 0.06f, 0.12f, 0.85f);
    colors[ImGuiCol_HeaderActive]         = NEON_RED_ACTIVE;

    // Frames (Inputs, Combo boxes, Sliders)
    colors[ImGuiCol_FrameBg]              = BG_FRAME;
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.13f, 0.13f, 0.17f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.18f, 0.04f, 0.08f, 1.00f);

    // Titles & Tabs
    colors[ImGuiCol_TitleBg]              = ImVec4(0.04f, 0.04f, 0.05f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.09f, 0.02f, 0.05f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
    colors[ImGuiCol_Tab]                  = ImVec4(0.09f, 0.09f, 0.12f, 1.00f);
    colors[ImGuiCol_TabHovered]           = NEON_RED_HOVER;
    colors[ImGuiCol_TabActive]            = NEON_RED;
    colors[ImGuiCol_TabUnfocused]         = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.12f, 0.02f, 0.05f, 1.00f);

    // Buttons (Cyberpunk Accents, #FF003C Active Accent)
    colors[ImGuiCol_Button]               = ImVec4(0.13f, 0.13f, 0.17f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.75f, 0.05f, 0.20f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = NEON_RED; // #FF003C

    // Sliders & Progress Bars (#FF003C CheckMark)
    colors[ImGuiCol_SliderGrab]           = NEON_RED;
    colors[ImGuiCol_SliderGrabActive]     = NEON_RED_HOVER;
    colors[ImGuiCol_CheckMark]            = NEON_RED; // #FF003C
    colors[ImGuiCol_PlotHistogram]        = NEON_GREEN;
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.00f, 1.00f, 0.60f, 1.00f);
    colors[ImGuiCol_PlotLines]            = NEON_CYAN;
    colors[ImGuiCol_PlotLinesHovered]     = NEON_RED;

    // Scrollbars
    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.04f, 0.04f, 0.05f, 0.40f);
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.18f, 0.18f, 0.24f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = NEON_RED_HOVER;
    colors[ImGuiCol_ScrollbarGrabActive]  = NEON_RED;

    // Separators & Resize Grips
    colors[ImGuiCol_Separator]            = BORDER_DARK;
    colors[ImGuiCol_SeparatorHovered]     = NEON_RED_HOVER;
    colors[ImGuiCol_SeparatorActive]      = NEON_RED;
    colors[ImGuiCol_ResizeGrip]           = ImVec4(0.15f, 0.02f, 0.06f, 0.80f);
    colors[ImGuiCol_ResizeGripHovered]    = NEON_RED_HOVER;
    colors[ImGuiCol_ResizeGripActive]     = NEON_RED;

    // Tables
    colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.09f, 0.09f, 0.12f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]    = BORDER_DARK;
    colors[ImGuiCol_TableBorderLight]     = ImVec4(0.10f, 0.10f, 0.13f, 0.50f);
    colors[ImGuiCol_TableRowBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]        = ImVec4(0.06f, 0.06f, 0.08f, 0.35f);

    // Modern Softened Geometry (WindowRounding & FrameRounding = 6.0f)
    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;

    // WindowPadding: ImVec2(16, 16) to let panels breathe
    style.WindowPadding     = ImVec2(16.0f, 16.0f);
    style.FramePadding      = ImVec2(10.0f, 6.0f);
    style.ItemSpacing       = ImVec2(10.0f, 10.0f);
    style.ItemInnerSpacing  = ImVec2(8.0f, 6.0f);
}

} // namespace CyberpunkTheme
