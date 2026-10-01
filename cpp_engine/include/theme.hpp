#pragma once

#include "imgui.h"

namespace CyberpunkTheme {
    // Brand Palette Specifications
    // WindowBg: Hex #0A0A0C
    constexpr ImVec4 BG_WINDOW          = ImVec4(0.039f, 0.039f, 0.047f, 1.00f);
    constexpr ImVec4 BG_DEEP_DARK       = BG_WINDOW;
    // ChildBg: Hex #121215
    constexpr ImVec4 BG_PANEL           = ImVec4(0.071f, 0.071f, 0.082f, 1.00f);
    // Inset Console Surface: Hex #050505
    constexpr ImVec4 BG_INSET           = ImVec4(0.020f, 0.020f, 0.020f, 1.00f);
    // Frame Background: Hex #16161B
    constexpr ImVec4 BG_FRAME           = ImVec4(0.086f, 0.086f, 0.106f, 1.00f);
    // Card Hover: Hex #1A1A22
    constexpr ImVec4 BG_CARD_HOVER      = ImVec4(0.102f, 0.102f, 0.133f, 1.00f);

    // Border: Hex #1E1E24
    constexpr ImVec4 BORDER_DARK        = ImVec4(0.118f, 0.118f, 0.141f, 1.00f);
    constexpr ImVec4 BORDER_LIGHT       = ImVec4(0.180f, 0.180f, 0.220f, 1.00f);

    // Neon Red Accent: Hex #FF003C
    constexpr ImVec4 NEON_RED           = ImVec4(1.000f, 0.000f, 0.235f, 1.00f);
    constexpr ImVec4 NEON_RED_HOVER     = ImVec4(1.000f, 0.200f, 0.400f, 1.00f);
    constexpr ImVec4 NEON_RED_ACTIVE    = ImVec4(0.700f, 0.000f, 0.165f, 1.00f);

    // Complementary Cyberpunk Accents
    constexpr ImVec4 NEON_CYAN          = ImVec4(0.000f, 0.941f, 1.000f, 1.00f); // #00F0FF
    constexpr ImVec4 NEON_GREEN         = ImVec4(0.000f, 1.000f, 0.400f, 1.00f); // #00FF66
    constexpr ImVec4 NEON_AMBER         = ImVec4(1.000f, 0.750f, 0.000f, 1.00f); // #FFBF00
    constexpr ImVec4 GOLD_ACCENT        = ImVec4(0.950f, 0.750f, 0.200f, 1.00f); // #F2C033
    constexpr ImVec4 BTC_ORANGE         = ImVec4(0.960f, 0.580f, 0.120f, 1.00f); // #F7931A

    constexpr ImVec4 TEXT_PRIMARY       = ImVec4(0.940f, 0.940f, 0.960f, 1.00f);
    constexpr ImVec4 TEXT_MUTED         = ImVec4(0.480f, 0.480f, 0.540f, 1.00f);

    // Typography Fonts (JetBrains Mono)
    extern ImFont* g_font_regular;    // 14px standard
    extern ImFont* g_font_bold_large; // 24px bold primary values
    extern ImFont* g_font_bold_med;   // 17px bold headers / tags
    extern ImFont* g_font_small;      // 12px telemetry / console

    void LoadFonts(ImGuiIO& io);
    void ApplyTheme();
}
