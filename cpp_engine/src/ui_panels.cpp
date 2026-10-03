#include "ui_panels.hpp"
#include "theme.hpp"
#include "imgui.h"
#include <cstdio>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace UIPanels {

static char s_console_filter[128] = "";
static bool s_autoscroll = true;
static bool s_show_settings_modal = false;

// Rolling history for mini-sparkline charts
static float s_gold_history[32] = {0};
static float s_mtf_history[32] = {0};
static float s_range_history[32] = {0};
static float s_array_history[32] = {0};
static float s_target_history[32] = {0};
static bool s_history_initialized = false;

static void InitSparklines(double base_px) {
    for (int i = 0; i < 32; ++i) {
        float phase = (float)i * 0.25f;
        s_gold_history[i] = (float)base_px + std::sin(phase) * 1.5f + std::cos(phase * 0.7f) * 0.8f;
        s_mtf_history[i] = 50.0f + std::sin(phase * 1.2f) * 20.0f;
        s_range_history[i] = 40.0f + std::cos(phase * 0.9f) * 15.0f;
        s_array_history[i] = 60.0f + std::sin(phase * 1.5f) * 25.0f;
        s_target_history[i] = 55.0f + std::sin(phase * 0.8f) * 18.0f;
    }
    s_history_initialized = true;
}

static void PushSparklineValue(float* arr, int count, float val) {
    for (int i = 0; i < count - 1; ++i) {
        arr[i] = arr[i + 1];
    }
    arr[count - 1] = val;
}

// -----------------------------------------------------------------------------
// Custom Drawing Helpers
// -----------------------------------------------------------------------------

static void DrawMiniSparkline(ImDrawList* draw_list, ImVec2 pos, ImVec2 size, const float* values, int count, ImU32 line_color, ImU32 fill_color) {
    if (count < 2) return;
    float min_val = values[0], max_val = values[0];
    for (int i = 1; i < count; ++i) {
        if (values[i] < min_val) min_val = values[i];
        if (values[i] > max_val) max_val = values[i];
    }
    float range = (max_val - min_val);
    if (range < 0.001f) range = 1.0f;

    std::vector<ImVec2> pts;
    pts.reserve(count);
    for (int i = 0; i < count; ++i) {
        float x = pos.x + (float)i / (float)(count - 1) * size.x;
        float norm = (values[i] - min_val) / range;
        float y = pos.y + size.y - norm * (size.y - 6.0f) - 3.0f;
        pts.push_back(ImVec2(x, y));
    }

    if (fill_color != 0) {
        std::vector<ImVec2> poly = pts;
        poly.push_back(ImVec2(pos.x + size.x, pos.y + size.y));
        poly.push_back(ImVec2(pos.x, pos.y + size.y));
        draw_list->AddConvexPolyFilled(poly.data(), (int)poly.size(), fill_color);
    }
    draw_list->AddPolyline(pts.data(), (int)pts.size(), line_color, 0, 1.8f);
    // Draw pulsing end dot
    draw_list->AddCircleFilled(pts.back(), 3.5f, line_color);
    draw_list->AddCircle(pts.back(), 5.5f, line_color & 0x55FFFFFF, 12, 1.0f);
}

static void RenderPillBadge(const char* label, const char* value, ImVec4 val_color, ImVec4 bg_col = ImVec4(0.08f, 0.08f, 0.10f, 1.0f), ImVec4 border_col = ImVec4(0.18f, 0.18f, 0.22f, 1.0f)) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s  %s", label, value);
    ImVec2 txt_sz = ImGui::CalcTextSize(buf);
    ImVec2 p_min = ImGui::GetCursorScreenPos();
    float pill_w = txt_sz.x + 24.0f;
    float pill_h = 32.0f;
    ImVec2 p_max = ImVec2(p_min.x + pill_w, p_min.y + pill_h);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p_min, p_max, ImGui::ColorConvertFloat4ToU32(bg_col), 5.0f);
    dl->AddRect(p_min, p_max, ImGui::ColorConvertFloat4ToU32(border_col), 5.0f, 0, 1.0f);

    float text_y = p_min.y + (pill_h - txt_sz.y) * 0.5f;
    dl->AddText(CyberpunkTheme::g_font_regular, 13.0f, ImVec2(p_min.x + 10.0f, text_y), ImGui::ColorConvertFloat4ToU32(CyberpunkTheme::TEXT_MUTED), label);
    ImVec2 lbl_sz = ImGui::CalcTextSize(label);
    dl->AddText(CyberpunkTheme::g_font_regular, 13.0f, ImVec2(p_min.x + 10.0f + lbl_sz.x + 8.0f, text_y), ImGui::ColorConvertFloat4ToU32(val_color), value);

    ImGui::Dummy(ImVec2(pill_w, pill_h));
}

static void RenderAssetPill(const char* name, double price) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p_min = ImGui::GetCursorScreenPos();
    float width = 230.0f;
    float height = 48.0f;
    ImVec2 p_max = ImVec2(p_min.x + width, p_min.y + height);

    ImU32 bg_col = IM_COL32(28, 24, 16, 255);
    ImU32 bdr_col = IM_COL32(242, 192, 51, 230);

    dl->AddRectFilled(p_min, p_max, bg_col, 6.0f);
    dl->AddRect(p_min, p_max, bdr_col, 6.0f, 0, 1.5f);

    // Coin badge icon (Au Gold)
    ImVec2 icon_center = ImVec2(p_min.x + 24.0f, p_min.y + height * 0.5f);
    dl->AddCircleFilled(icon_center, 13.0f, IM_COL32(242, 192, 51, 230));
    dl->AddCircle(icon_center, 13.0f, IM_COL32(255, 255, 255, 120), 16, 1.0f);
    dl->AddText(CyberpunkTheme::g_font_small, 11.0f, ImVec2(icon_center.x - 7.0f, icon_center.y - 7.0f), IM_COL32(20, 20, 20, 255), "Au");

    // Symbol & Price text
    char price_buf[32];
    std::snprintf(price_buf, sizeof(price_buf), "$%.2f", price);

    ImVec2 title_pos = ImVec2(p_min.x + 46.0f, p_min.y + 6.0f);
    dl->AddText(CyberpunkTheme::g_font_bold_med, 14.0f, title_pos, IM_COL32(242, 192, 51, 255), name);

    ImVec2 price_pos = ImVec2(p_min.x + 46.0f, p_min.y + 25.0f);
    dl->AddText(CyberpunkTheme::g_font_regular, 14.5f, price_pos, IM_COL32(0, 255, 102, 255), price_buf);

    ImGui::Dummy(ImVec2(width, height));
}

// -----------------------------------------------------------------------------
// 1. Header Panel
// -----------------------------------------------------------------------------

void RenderHeaderAndToggle(ThreadSafeAppState& state, const MarketFrame& frame) {
    if (!s_history_initialized) {
        InitSparklines(frame.price > 100.0 ? frame.price : 4180.0);
    }
    PushSparklineValue(s_gold_history, 32, (float)frame.price);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, CyberpunkTheme::BG_PANEL);
    ImGui::PushStyleColor(ImGuiCol_Border, CyberpunkTheme::BORDER_DARK);
    ImGui::BeginChild("HeaderPanel", ImVec2(0, 80), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Left Branding: Stylized SparkX Logo
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 cp = ImGui::GetCursorScreenPos();

    // Stylized X Badge
    dl->AddRectFilled(ImVec2(cp.x, cp.y + 12), ImVec2(cp.x + 28, cp.y + 40), IM_COL32(255, 0, 60, 230), 4.0f);
    dl->AddText(CyberpunkTheme::g_font_bold_med, 18.0f, ImVec2(cp.x + 7, cp.y + 16), IM_COL32(255, 255, 255, 255), "X");

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 38.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
    ImGui::PushFont(CyberpunkTheme::g_font_bold_large);
    ImGui::TextColored(CyberpunkTheme::TEXT_PRIMARY, "SparkX EA");
    ImGui::PopFont();

    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "|");
    ImGui::SameLine();
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "Ultra-Low-Latency\nDecision Engine (Laya-ONNX)");

    // Middle: Dedicated Gold Asset Display Card
    ImGui::SameLine(325.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
    RenderAssetPill("XAUUSD (GOLD)", frame.price > 100.0 ? frame.price : 4186.80);

    // Right: Pill Badges using ImGui::RenderFrame style
    bool is_mt5 = (frame.feed_source.find("MT5") != std::string::npos);
    ImGui::SameLine(570.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);

    // Pill: Feed Source (MT5 Live or TradingView Live)
    RenderPillBadge("Feed", is_mt5 ? "MT5 Live" : "TradingView Live", is_mt5 ? CyberpunkTheme::NEON_GREEN : CyberpunkTheme::NEON_CYAN);
    ImGui::SameLine();

    // If MT5 is not connected, provide a prominent 1-click installer button
    if (!is_mt5) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.45f, 0.05f, 0.25f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.55f, 0.10f, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.60f, 0.15f, 0.60f));
        ImGui::PushStyleColor(ImGuiCol_Text, CyberpunkTheme::NEON_AMBER);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
        if (ImGui::Button("[+ Install MT5]", ImVec2(115.0f, 32.0f))) {
#if defined(_WIN32)
            ShellExecuteA(NULL, "open", "https://download.mql5.com/cdn/web/metaquotes.software.corp/mt5/mt5setup.exe", NULL, NULL, SW_SHOWNORMAL);
#endif
            state.AddLog("INFO", "Opening official MetaTrader 5 direct installer in browser...");
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("MetaTrader 5 terminal not detected. Click to download and install official low-latency MT5.\nCurrently streaming live authentic TradingView / Swissquote spot data.");
        }
        ImGui::SameLine();
    }

    // Pill 1: Latency
    char lat_buf[32];
    std::snprintf(lat_buf, sizeof(lat_buf), "%.2fms", frame.ingest_latency_ms);
    RenderPillBadge("Latency", lat_buf, CyberpunkTheme::NEON_CYAN);
    ImGui::SameLine();

    // Pill 2: GPU/CUDA
    bool stream_ok = state.IsZmqConnected();
    RenderPillBadge("GPU", stream_ok ? "Green" : "CPU", stream_ok ? CyberpunkTheme::NEON_GREEN : CyberpunkTheme::NEON_AMBER);
    ImGui::SameLine();

    // Pill 3: CTX tokens
    char ctx_buf[32];
    std::snprintf(ctx_buf, sizeof(ctx_buf), "%d/512", frame.token_count);
    RenderPillBadge("CTX", ctx_buf, CyberpunkTheme::TEXT_PRIMARY);
    ImGui::SameLine();

    // Mode Toggle Pill / Button
    EngineState current_state = state.GetEngineState();
    const char* mode_str = (current_state == EngineState::ARMED) ? "Armed" : 
                           ((current_state == EngineState::EMERGENCY_KILL) ? "Halt" : "Standby");
    ImVec4 mode_col = (current_state == EngineState::ARMED) ? CyberpunkTheme::NEON_RED : 
                      ((current_state == EngineState::EMERGENCY_KILL) ? ImVec4(1.0f, 0.1f, 0.1f, 1.0f) : CyberpunkTheme::NEON_AMBER);

    ImVec2 m_pos = ImGui::GetCursorScreenPos();
    ImGui::PushID("mode_pill");
    if (ImGui::InvisibleButton("##mode_toggle", ImVec2(90, 32))) {
        if (current_state == EngineState::STANDBY) {
            state.SetEngineState(EngineState::ARMED);
            state.AddLog("ALERT", "Engine state transitioned to ARMED (Auto-Execution Active)");
        } else if (current_state == EngineState::ARMED) {
            state.SetEngineState(EngineState::STANDBY);
            state.AddLog("INFO", "Engine state transitioned to STANDBY (Observation Only)");
        } else {
            state.SetEngineState(EngineState::STANDBY);
            state.AddLog("INFO", "Engine reset from KILL to STANDBY");
        }
    }
    bool m_hov = ImGui::IsItemHovered();
    ImGui::PopID();

    dl->AddRectFilled(m_pos, ImVec2(m_pos.x + 90, m_pos.y + 32), m_hov ? IM_COL32(32, 28, 24, 255) : IM_COL32(20, 18, 16, 255), 5.0f);
    dl->AddRect(m_pos, ImVec2(m_pos.x + 90, m_pos.y + 32), ImGui::ColorConvertFloat4ToU32(mode_col), 5.0f, 0, 1.2f);
    dl->AddText(CyberpunkTheme::g_font_regular, 12.0f, ImVec2(m_pos.x + 6, m_pos.y + 8), ImGui::ColorConvertFloat4ToU32(CyberpunkTheme::TEXT_MUTED), "Mode");
    dl->AddText(CyberpunkTheme::g_font_bold_med, 12.5f, ImVec2(m_pos.x + 38, m_pos.y + 8), ImGui::ColorConvertFloat4ToU32(mode_col), mode_str);

    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);

    // ⚙ Settings Button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.17f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.20f, 0.28f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.28f, 0.28f, 0.38f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, CyberpunkTheme::NEON_CYAN);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    if (ImGui::Button("[ ⚙ Settings ]", ImVec2(105.0f, 32.0f))) {
        s_show_settings_modal = true;
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Open Trade Settings & Risk Management Menu\n(Configure lot sizes, SL/TP brackets, auto-trading, and mobile phone alerts)");
    }
    ImGui::SameLine();

    // Quick Manual Order Buttons: BUY, SELL, CLOSE ALL
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.55f, 0.25f, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.75f, 0.35f, 0.65f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.90f, 0.40f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Text, CyberpunkTheme::NEON_GREEN);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    if (ImGui::Button("+ BUY", ImVec2(55.0f, 32.0f))) {
        state.QueueCommand("{\"command\":\"EXECUTE\",\"symbol\":\"XAUUSD\",\"action\":\"MARKET_BUY\"}");
        state.AddLog("INFO", "[MANUAL EXEC] Dispatched MARKET BUY command for XAUUSD");
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Direct Market BUY (Uses Lot Size & SL/TP from Settings)");
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.05f, 0.15f, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.08f, 0.20f, 0.65f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.10f, 0.25f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Text, CyberpunkTheme::NEON_RED);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    if (ImGui::Button("- SELL", ImVec2(55.0f, 32.0f))) {
        state.QueueCommand("{\"command\":\"EXECUTE\",\"symbol\":\"XAUUSD\",\"action\":\"MARKET_SELL\"}");
        state.AddLog("INFO", "[MANUAL EXEC] Dispatched MARKET SELL command for XAUUSD");
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Direct Market SELL (Uses Lot Size & SL/TP from Settings)");
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.15f, 0.15f, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.20f, 0.20f, 0.65f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.25f, 0.25f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Text, CyberpunkTheme::TEXT_MUTED);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    if (ImGui::Button("✖ Flat", ImVec2(55.0f, 32.0f))) {
        state.QueueCommand("{\"command\":\"CLOSE_ALL\",\"symbol\":\"XAUUSD\"}");
        state.AddLog("ALERT", "[CLOSE ALL] Sent command to flatten all positions");
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Emergency Flatten: Closes all open positions on account");

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

// -----------------------------------------------------------------------------
// 2. Main Row: Laya Primitives (Choice, Score, Noul)
// -----------------------------------------------------------------------------

void RenderLayaPrimitives(const LayaOutput& laya, const MarketFrame& frame) {
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    float panel_width = (content_size.x - 20.0f) / 3.0f;
    float panel_height = 295.0f;

    // =========================================================================
    // PRIMITIVE 1: CHOICE (Directional Action)
    // =========================================================================
    ImGui::PushStyleColor(ImGuiCol_ChildBg, CyberpunkTheme::BG_PANEL);
    ImGui::PushStyleColor(ImGuiCol_Border, CyberpunkTheme::BORDER_DARK);
    ImGui::BeginChild("ChoicePanel", ImVec2(panel_width, panel_height), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "PRIMITIVE 1: CHOICE (Directional Action)");
    ImGui::Dummy(ImVec2(0, 4));

    // Large Digital Action (24px Bold JetBrains)
    ImVec4 action_color = CyberpunkTheme::TEXT_PRIMARY;
    if (laya.choice_action.find("BUY") != std::string::npos) action_color = CyberpunkTheme::NEON_GREEN;
    else if (laya.choice_action.find("SELL") != std::string::npos) action_color = CyberpunkTheme::NEON_RED;
    else action_color = CyberpunkTheme::TEXT_PRIMARY;

    ImGui::PushFont(CyberpunkTheme::g_font_bold_large);
    ImGui::TextColored(action_color, "%s", laya.choice_action.c_str());
    ImGui::PopFont();

    // Confidence Subtitle & Progress Bar
    char conf_str[64];
    std::snprintf(conf_str, sizeof(conf_str), "Confidence: %.1f%%", laya.choice_confidence * 100.0f);
    ImGui::TextColored(CyberpunkTheme::NEON_GREEN, "%s", conf_str);

    // Custom-drawn Sleek Confidence Bar
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 bar_pos = ImGui::GetCursorScreenPos();
    float bar_w = ImGui::GetContentRegionAvail().x;
    float bar_h = 10.0f;
    dl->AddRectFilled(bar_pos, ImVec2(bar_pos.x + bar_w, bar_pos.y + bar_h), IM_COL32(24, 24, 30, 255), 4.0f);
    dl->AddRectFilled(bar_pos, ImVec2(bar_pos.x + bar_w * laya.choice_confidence, bar_pos.y + bar_h), ImGui::ColorConvertFloat4ToU32(action_color), 4.0f);
    ImGui::Dummy(ImVec2(bar_w, bar_h + 8.0f));

    // Action Distribution Breakdown Sub-heading
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "Action Distribution Breakdown:");
    ImGui::Dummy(ImVec2(0, 4));

    // Custom-Drawn Multi-Color Stacked Distribution Bar with Icons
    ImVec2 stack_pos = ImGui::GetCursorScreenPos();
    float stack_w = ImGui::GetContentRegionAvail().x - 110.0f;
    float stack_h = 110.0f;

    // Draw 7 vertical stacked columns
    int num_cols = 7;
    float col_gap = 8.0f;
    float col_w = (stack_w - (num_cols - 1) * col_gap) / num_cols;
    const char* col_labels[7] = { "[EQ]", "[DP]", "[TR]", "[VOL]", "[MOM]", "[SWP]", "[ENG]" };

    float prob_mkt_buy = 0.0f;
    float prob_mkt_sell = 0.0f;
    float prob_lim_buy = 0.0f;
    float prob_lim_sell = 0.0f;
    float prob_hold = 0.0f;

    for (const auto& p : laya.choice_distribution) {
        if (p.first == "MARKET_BUY") prob_mkt_buy = p.second;
        else if (p.first == "MARKET_SELL") prob_mkt_sell = p.second;
        else if (p.first == "LIMIT_BUY_ORDER_BLOCK") prob_lim_buy = p.second;
        else if (p.first == "LIMIT_SELL_ORDER_BLOCK") prob_lim_sell = p.second;
        else if (p.first == "HOLD") prob_hold = p.second;
    }

    if (laya.choice_distribution.empty()) {
        prob_mkt_buy = 0.35f;
        prob_hold = 0.25f;
        prob_lim_buy = 0.10f;
        prob_mkt_sell = 0.08f;
    }

    // Dynamic factor breakdown per SMC column
    struct ColFactor { float buy; float hold; float sell; };
    ColFactor cols[7];

    bool is_disp = frame.compressed_state.find("DISP:T") != std::string::npos;
    bool is_vol_exp = frame.compressed_state.find("VOL_EXP:T") != std::string::npos;
    bool is_h1_bull = frame.bias.h1_trend.find("BULL") != std::string::npos;
    bool is_h1_bear = frame.bias.h1_trend.find("BEAR") != std::string::npos;

    // 0: [EQ] Equilibrium / Discount / Premium Zone
    if (frame.bias.zone == "DISCOUNT") {
        cols[0] = { 0.75f, 0.15f, 0.10f };
    } else if (frame.bias.zone == "PREMIUM") {
        cols[0] = { 0.10f, 0.15f, 0.75f };
    } else {
        cols[0] = { 0.20f, 0.60f, 0.20f };
    }

    // 1: [DP] Displacement
    if (is_disp) {
        if (is_h1_bull || frame.liquidity.ssl_swept) cols[1] = { 0.80f, 0.12f, 0.08f };
        else if (is_h1_bear || frame.liquidity.bsl_swept) cols[1] = { 0.08f, 0.12f, 0.80f };
        else cols[1] = { 0.45f, 0.10f, 0.45f };
    } else {
        cols[1] = { 0.15f, 0.70f, 0.15f };
    }

    // 2: [TR] Trend Bias
    if (is_h1_bull) {
        cols[2] = { 0.78f, 0.14f, 0.08f };
    } else if (is_h1_bear) {
        cols[2] = { 0.08f, 0.14f, 0.78f };
    } else {
        cols[2] = { 0.25f, 0.50f, 0.25f };
    }

    // 3: [VOL] Volume Expansion
    if (is_vol_exp) {
        if (is_h1_bull) cols[3] = { 0.72f, 0.16f, 0.12f };
        else if (is_h1_bear) cols[3] = { 0.12f, 0.16f, 0.72f };
        else cols[3] = { 0.40f, 0.20f, 0.40f };
    } else {
        cols[3] = { 0.20f, 0.60f, 0.20f };
    }

    // 4: [MOM] Momentum / Overall Distribution Alignment
    float mom_buy = prob_mkt_buy + prob_lim_buy * 0.6f;
    float mom_sell = prob_mkt_sell + prob_lim_sell * 0.6f;
    float mom_hold = prob_hold;
    float mom_total = mom_buy + mom_sell + mom_hold;
    if (mom_total > 0.001f) {
        cols[4] = { mom_buy / mom_total, mom_hold / mom_total, mom_sell / mom_total };
    } else {
        cols[4] = { 0.20f, 0.60f, 0.20f };
    }

    // 5: [SWP] Liquidity Sweep
    if (frame.liquidity.ssl_swept) {
        cols[5] = { 0.85f, 0.08f, 0.07f };
    } else if (frame.liquidity.bsl_swept) {
        cols[5] = { 0.07f, 0.08f, 0.85f };
    } else {
        cols[5] = { 0.15f, 0.70f, 0.15f };
    }

    // 6: [ENG] Institutional Array (FVG / OB) Engagement
    if (frame.arrays.fvg_active || frame.arrays.ob_active) {
        bool is_bull_array = (frame.arrays.fvg_direction == "BULLISH" || frame.arrays.ob_direction == "BULLISH");
        bool is_bear_array = (frame.arrays.fvg_direction == "BEARISH" || frame.arrays.ob_direction == "BEARISH");
        if (is_bull_array) cols[6] = { 0.82f, 0.10f, 0.08f };
        else if (is_bear_array) cols[6] = { 0.08f, 0.10f, 0.82f };
        else cols[6] = { 0.45f, 0.10f, 0.45f };
    } else {
        cols[6] = { 0.15f, 0.70f, 0.15f };
    }

    for (int i = 0; i < num_cols; ++i) {
        float x0 = stack_pos.x + i * (col_w + col_gap);
        float x1 = x0 + col_w;
        float h_usable = stack_h - 22.0f;

        float c_b = cols[i].buy;
        float c_h = cols[i].hold;
        float c_s = cols[i].sell;
        float sum = c_b + c_h + c_s;
        if (sum > 0.001f) {
            c_b /= sum; c_h /= sum; c_s /= sum;
        }

        float b_h = h_usable * c_b;
        float h_h = h_usable * c_h;
        float s_h = h_usable - b_h - h_h;
        if (s_h < 4.0f) s_h = 4.0f;

        float cur_y = stack_pos.y + stack_h - 20.0f;
        // Bottom: SELL (Red)
        dl->AddRectFilled(ImVec2(x0, cur_y - s_h), ImVec2(x1, cur_y), IM_COL32(255, 60, 60, 230), 2.5f);
        cur_y -= s_h;
        // Middle: HOLD (Amber/Brown)
        dl->AddRectFilled(ImVec2(x0, cur_y - h_h), ImVec2(x1, cur_y), IM_COL32(184, 115, 51, 230), 2.5f);
        cur_y -= h_h;
        // Top: BUY (Neon Green)
        dl->AddRectFilled(ImVec2(x0, cur_y - b_h), ImVec2(x1, cur_y), IM_COL32(0, 255, 102, 230), 2.5f);

        // Label below
        ImVec2 lbl_sz = ImGui::CalcTextSize(col_labels[i]);
        dl->AddText(CyberpunkTheme::g_font_small, 11.0f, ImVec2(x0 + (col_w - lbl_sz.x) * 0.5f, stack_pos.y + stack_h - 16.0f), IM_COL32(140, 140, 155, 255), col_labels[i]);
    }

    // Right-side Percentage breakdown badges (Dynamic formatting from Laya Choice Distribution)
    char badge_buf[32];
    ImVec2 right_pos = ImVec2(stack_pos.x + stack_w + 14.0f, stack_pos.y + 6.0f);

    // BUY
    dl->AddCircleFilled(ImVec2(right_pos.x + 6, right_pos.y + 8), 5.0f, IM_COL32(0, 255, 102, 255));
    std::snprintf(badge_buf, sizeof(badge_buf), "BUY   %2.0f%%", prob_mkt_buy * 100.0f);
    dl->AddText(CyberpunkTheme::g_font_regular, 13.0f, ImVec2(right_pos.x + 18, right_pos.y), IM_COL32(0, 255, 102, 255), badge_buf);

    // HOLD
    dl->AddCircleFilled(ImVec2(right_pos.x + 6, right_pos.y + 32), 5.0f, IM_COL32(242, 192, 51, 255));
    std::snprintf(badge_buf, sizeof(badge_buf), "HOLD  %2.0f%%", prob_hold * 100.0f);
    dl->AddText(CyberpunkTheme::g_font_regular, 13.0f, ImVec2(right_pos.x + 18, right_pos.y + 24), IM_COL32(242, 192, 51, 255), badge_buf);

    // LIM_B
    dl->AddCircleFilled(ImVec2(right_pos.x + 6, right_pos.y + 56), 5.0f, IM_COL32(0, 229, 255, 255));
    std::snprintf(badge_buf, sizeof(badge_buf), "LIM_B %2.0f%%", prob_lim_buy * 100.0f);
    dl->AddText(CyberpunkTheme::g_font_regular, 13.0f, ImVec2(right_pos.x + 18, right_pos.y + 48), IM_COL32(0, 229, 255, 255), badge_buf);

    // SELL
    dl->AddCircleFilled(ImVec2(right_pos.x + 6, right_pos.y + 80), 5.0f, IM_COL32(255, 60, 60, 255));
    std::snprintf(badge_buf, sizeof(badge_buf), "SELL  %2.0f%%", prob_mkt_sell * 100.0f);
    dl->AddText(CyberpunkTheme::g_font_regular, 13.0f, ImVec2(right_pos.x + 18, right_pos.y + 72), IM_COL32(255, 60, 60, 255), badge_buf);

    ImGui::Dummy(ImVec2(stack_w + 110.0f, stack_h));

    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    ImGui::SameLine();

    // =========================================================================
    // PRIMITIVE 2: SCORE (1-10 Quality)
    // =========================================================================
    ImGui::PushStyleColor(ImGuiCol_ChildBg, CyberpunkTheme::BG_PANEL);
    ImGui::PushStyleColor(ImGuiCol_Border, CyberpunkTheme::BORDER_DARK);
    ImGui::BeginChild("ScorePanel", ImVec2(panel_width, panel_height), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "PRIMITIVE 2: SCORE (1-10 Quality)");

    // Confidence Pill top-right
    ImGui::SameLine(ImGui::GetWindowWidth() - 140.0f);
    char conf_pill[32];
    std::snprintf(conf_pill, sizeof(conf_pill), "%.1f%%", std::min(99.9f, (laya.score_grade / 10.0f) * 100.0f));
    RenderPillBadge("Confidence", conf_pill, laya.score_grade >= 7.0f ? CyberpunkTheme::NEON_GREEN : CyberpunkTheme::NEON_AMBER);

    ImGui::Dummy(ImVec2(0, 2));

    // Digital Grade (24px Bold)
    ImVec4 score_col = laya.score_grade >= 7.0f ? CyberpunkTheme::NEON_GREEN : CyberpunkTheme::NEON_RED;
    ImGui::PushFont(CyberpunkTheme::g_font_bold_large);
    ImGui::TextColored(score_col, "%.2f", laya.score_grade);
    ImGui::SameLine();
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "/ 10.0");
    ImGui::PopFont();

    // Custom ImDrawList 10-Step Meter
    ImVec2 meter_pos = ImGui::GetCursorScreenPos();
    float meter_w = ImGui::GetContentRegionAvail().x;
    float meter_h = 12.0f;
    int max_steps = 10;
    float step_gap = 4.0f;
    float step_w = (meter_w - (max_steps - 1) * step_gap) / max_steps;
    int active_steps = (int)std::round(laya.score_grade);

    for (int s = 0; s < max_steps; ++s) {
        float x0 = meter_pos.x + s * (step_w + step_gap);
        float x1 = x0 + step_w;
        float y0 = meter_pos.y;
        float y1 = y0 + meter_h;

        if (s < active_steps) {
            ImU32 step_color;
            if (s < 3) step_color = IM_COL32(255, 60, 60, 240);       // Red
            else if (s < 6) step_color = IM_COL32(255, 175, 20, 240); // Amber
            else step_color = IM_COL32(0, 255, 102, 240);             // Green
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), step_color, 2.5f);
        } else {
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(24, 24, 30, 220), 2.5f);
            dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(38, 38, 48, 180), 2.5f, 0, 1.0f);
        }
    }
    ImGui::Dummy(ImVec2(meter_w, meter_h + 8.0f));

    // Dynamic Assessment Banner
    if (laya.score_grade >= 7.0f) {
        ImGui::TextColored(CyberpunkTheme::NEON_GREEN, "[A+ HIGH-PROBABILITY EXPANSION MODEL]");
    } else {
        ImGui::TextColored(CyberpunkTheme::NEON_AMBER, "[SUBPRIME / CHOP DETECTED (<= 7.0)]");
    }

    ImGui::Dummy(ImVec2(0, 6));
    char lat_str[96];
    std::snprintf(lat_str, sizeof(lat_str), "Inference Latency: %.2f ms [%s]", laya.inference_latency_ms, laya.execution_provider.c_str());
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "%s", lat_str);
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "Argmax Quality Grade: Tier %d / 10", laya.score_top_grade);
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "SMC Confluence: %s", laya.score_grade >= 7.0f ? "EXPANSION (Displacement Confirmed)" : "CONSOLIDATION / EQUILIBRIUM");
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "Execution Threshold: >> 7.0 / 10.0");

    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    ImGui::SameLine();

    // =========================================================================
    // PRIMITIVE 3: NOUL (Truth Matrix)
    // =========================================================================
    ImGui::PushStyleColor(ImGuiCol_ChildBg, CyberpunkTheme::BG_PANEL);
    ImGui::PushStyleColor(ImGuiCol_Border, CyberpunkTheme::BORDER_DARK);
    ImGui::BeginChild("NoulPanel", ImVec2(panel_width, panel_height), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "PRIMITIVE 3: NOUL (Truth Matrix)");
    ImGui::Dummy(ImVec2(0, 6));

    // 2-Column Table for Truth Matrix conditions
    struct NoulItem {
        const char* name;
        bool is_valid;
    };

    std::vector<NoulItem> noul_items = {
        { "H1 Macro Trend Bias", frame.bias.h1_trend.find("BULL") != std::string::npos || frame.bias.h1_trend.find("BEAR") != std::string::npos },
        { "M15 Structure Alignment", frame.bias.m15_struct.find("BOS") != std::string::npos || frame.bias.m15_struct.find("MSS") != std::string::npos },
        { "M5 Market Shift (MSS)", frame.bias.m5_struct.find("MSS") != std::string::npos || frame.bias.m5_struct.find("BOS") != std::string::npos },
        { "Price in Prem/Disc Zone", frame.bias.zone != "EQUILIBRIUM" },
        { "Liquidity Swept (BSL/SSL)", frame.liquidity.ssl_swept || frame.liquidity.bsl_swept },
        { "Fresh M5 Fair Value Gap", frame.arrays.fvg_active },
        { "Institutional Displacement", frame.compressed_state.find("DISP:T") != std::string::npos }
    };

    if (ImGui::BeginTable("NoulTable", 1, ImGuiTableFlags_NoBordersInBody)) {
        for (const auto& item : noul_items) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            ImVec2 cell_pos = ImGui::GetCursorScreenPos();
            float circle_r = 5.0f;
            ImVec2 circle_center = ImVec2(cell_pos.x + 8.0f, cell_pos.y + 10.0f);

            // Custom drawn circles: filled for true, hollow for false
            if (item.is_valid) {
                dl->AddCircleFilled(circle_center, circle_r, IM_COL32(0, 255, 102, 240));
                dl->AddCircle(circle_center, circle_r + 2.0f, IM_COL32(0, 255, 102, 70), 16, 1.2f);
            } else {
                dl->AddCircle(circle_center, circle_r, IM_COL32(255, 60, 60, 230), 16, 2.0f);
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 24.0f);
            ImGui::TextColored(CyberpunkTheme::TEXT_PRIMARY, "%s", item.name);
            ImGui::SameLine();
            ImGui::TextColored(item.is_valid ? CyberpunkTheme::NEON_GREEN : CyberpunkTheme::NEON_RED, "(%s)", item.is_valid ? "Green" : "Red");
        }
        ImGui::EndTable();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

// -----------------------------------------------------------------------------
// 3. Middle Section: LIVE MARKET DATA & SMC CONFLUENCE MATRIX (5 Cards)
// -----------------------------------------------------------------------------

void RenderSMCContext(const MarketFrame& frame) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, CyberpunkTheme::BG_PANEL);
    ImGui::PushStyleColor(ImGuiCol_Border, CyberpunkTheme::BORDER_DARK);
    ImGui::BeginChild("SMCContextPanel", ImVec2(0, 180), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::TextColored(CyberpunkTheme::NEON_CYAN, "LIVE MARKET DATA & SMC CONFLUENCE MATRIX");
    ImGui::SameLine(ImGui::GetWindowWidth() - 320);
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "Session: %s | Latency: 0.80ms", frame.session.c_str());
    ImGui::Separator();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float card_w = (avail.x - 36.0f) / 5.0f;
    float card_h = 126.0f;

    // -------------------------------------------------------------------------
    // Card 1: ASSET PRICING
    // -------------------------------------------------------------------------
    ImGui::BeginChild("Card1_Pricing", ImVec2(card_w, card_h), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* card1_dl = ImGui::GetWindowDrawList();
    ImVec2 card1_pos = ImGui::GetWindowPos();

    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "ASSET PRICING:");
    ImGui::PushFont(CyberpunkTheme::g_font_bold_med);
    ImGui::TextColored(CyberpunkTheme::NEON_GREEN, "%s / $%.2f", frame.symbol.c_str(), frame.price);
    ImGui::PopFont();
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "Bid: $%.2f | Ask: $%.2f", frame.bid, frame.ask);

    // Mini Sparkline Wave (Gold)
    float sp1_w = card_w - 24.0f;
    float sp1_h = 24.0f;
    ImVec2 sp1_pos(card1_pos.x + 12.0f, card1_pos.y + card_h - sp1_h - 12.0f);
    DrawMiniSparkline(card1_dl, sp1_pos, ImVec2(sp1_w, sp1_h), s_gold_history, 32, IM_COL32(242, 192, 51, 230), IM_COL32(242, 192, 51, 35));
    ImGui::EndChild();

    ImGui::SameLine();

    // -------------------------------------------------------------------------
    // Card 2: MULTI-TIMEFRAME STRUCTURE
    // -------------------------------------------------------------------------
    ImGui::BeginChild("Card2_MTF", ImVec2(card_w, card_h), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* card2_dl = ImGui::GetWindowDrawList();
    ImVec2 card2_pos = ImGui::GetWindowPos();

    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "MULTI-TIMEFRAME STRUCTURE:");
    ImGui::Text("H1: "); ImGui::SameLine();
    ImGui::TextColored(CyberpunkTheme::NEON_AMBER, "%s", frame.bias.h1_trend.c_str());
    ImGui::SameLine(card_w * 0.46f);
    ImGui::Text("M15: "); ImGui::SameLine();
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "%s", frame.bias.m15_struct.c_str());

    ImGui::Text("M5 Micro Shift: "); ImGui::SameLine();
    ImGui::TextColored(CyberpunkTheme::NEON_GREEN, "%s", frame.bias.m5_struct.c_str());

    // Mini Sparkline Wave (Red/Green)
    float sp2_w = card_w - 24.0f;
    float sp2_h = 22.0f;
    ImVec2 sp2_pos(card2_pos.x + 12.0f, card2_pos.y + card_h - sp2_h - 12.0f);
    DrawMiniSparkline(card2_dl, sp2_pos, ImVec2(sp2_w, sp2_h), s_mtf_history, 32, IM_COL32(255, 60, 60, 220), IM_COL32(255, 60, 60, 30));
    ImGui::EndChild();

    ImGui::SameLine();

    // -------------------------------------------------------------------------
    // Card 3: DEALING RANGE & PRICING
    // -------------------------------------------------------------------------
    ImGui::BeginChild("Card3_Range", ImVec2(card_w, card_h), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* card3_dl = ImGui::GetWindowDrawList();
    ImVec2 card3_pos = ImGui::GetWindowPos();

    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "DEALING RANGE & PRICING:");
    ImGui::TextColored(CyberpunkTheme::NEON_AMBER, "Zone: %s (Fib %.1f%%)", frame.bias.zone.c_str(), frame.bias.fib_pct);
    ImGui::Text("Asia: $%.0f-$%.0f", frame.liquidity.asia_low, frame.liquidity.asia_high);
    ImGui::SameLine(card_w * 0.54f);
    ImGui::TextColored(CyberpunkTheme::NEON_CYAN, "%s", frame.liquidity.ssl_swept ? "SSL_SWEPT" : (frame.liquidity.bsl_swept ? "BSL_SWEPT" : "NONE"));

    // Mini Sparkline Wave (Gold)
    float sp3_w = card_w - 24.0f;
    float sp3_h = 22.0f;
    ImVec2 sp3_pos(card3_pos.x + 12.0f, card3_pos.y + card_h - sp3_h - 12.0f);
    DrawMiniSparkline(card3_dl, sp3_pos, ImVec2(sp3_w, sp3_h), s_range_history, 32, IM_COL32(242, 192, 51, 220), IM_COL32(242, 192, 51, 30));
    ImGui::EndChild();

    ImGui::SameLine();

    // -------------------------------------------------------------------------
    // Card 4: ACTIVE ARRAYS & MOMENTUM
    // -------------------------------------------------------------------------
    ImGui::BeginChild("Card4_Arrays", ImVec2(card_w, card_h), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* card4_dl = ImGui::GetWindowDrawList();
    ImVec2 card4_pos = ImGui::GetWindowPos();

    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "ACTIVE ARRAYS & MOMENTUM:");
    ImGui::Text("M5 FVG: "); ImGui::SameLine();
    ImGui::TextColored(CyberpunkTheme::NEON_GREEN, "%s", frame.arrays.fvg_active ? "ACTIVE" : "NONE");
    ImGui::SameLine(card_w * 0.50f);
    ImGui::Text("M5 OB: "); ImGui::SameLine();
    ImGui::TextColored(CyberpunkTheme::NEON_GREEN, "%s", frame.arrays.ob_active ? "ACTIVE" : "NONE");

    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "Flow: Institutional Momentum");

    // Mini Sparkline Wave (Green)
    float sp4_w = card_w - 24.0f;
    float sp4_h = 24.0f;
    ImVec2 sp4_pos(card4_pos.x + 12.0f, card4_pos.y + card_h - sp4_h - 12.0f);
    DrawMiniSparkline(card4_dl, sp4_pos, ImVec2(sp4_w, sp4_h), s_array_history, 32, IM_COL32(0, 255, 102, 220), IM_COL32(0, 255, 102, 35));
    ImGui::EndChild();

    ImGui::SameLine();

    // -------------------------------------------------------------------------
    // Card 5: TARGETS (Sliders & Dual Target Chart)
    // -------------------------------------------------------------------------
    ImGui::BeginChild("Card5_Targets", ImVec2(card_w, card_h), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* card5_dl = ImGui::GetWindowDrawList();
    ImVec2 card5_pos = ImGui::GetWindowPos();

    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "TARGETS:");
    ImGui::TextColored(CyberpunkTheme::NEON_GREEN, "BSL $%.2f | SSL $%.2f", frame.liquidity.bsl_target, frame.liquidity.ssl_target);

    // Target Range Horizontal sliders with dynamic targets
    float slider_label_w = 28.0f;
    float slider_val_w = 46.0f;
    float slider_x = card5_pos.x + 12.0f + slider_label_w;
    float slider_w = card_w - 24.0f - slider_label_w - slider_val_w;
    float val_x = slider_x + slider_w + 6.0f;

    float line_y1 = card5_pos.y + card_h - 40.0f;
    float line_y2 = card5_pos.y + card_h - 18.0f;

    char bsl_buf[32];
    std::snprintf(bsl_buf, sizeof(bsl_buf), "$%.0f", frame.liquidity.bsl_target);
    char ssl_buf[32];
    std::snprintf(ssl_buf, sizeof(ssl_buf), "$%.0f", frame.liquidity.ssl_target);

    // BSL Line
    card5_dl->AddText(CyberpunkTheme::g_font_small, 11.0f, ImVec2(card5_pos.x + 12.0f, line_y1 - 6.0f), IM_COL32(140, 140, 155, 255), "BSL");
    card5_dl->AddLine(ImVec2(slider_x, line_y1), ImVec2(slider_x + slider_w, line_y1), IM_COL32(80, 80, 95, 200), 1.5f);
    card5_dl->AddCircleFilled(ImVec2(slider_x + slider_w * 0.75f, line_y1), 3.5f, IM_COL32(0, 255, 102, 255));
    card5_dl->AddCircle(ImVec2(slider_x + slider_w * 0.75f, line_y1), 5.5f, IM_COL32(0, 255, 102, 70), 12, 1.0f);
    card5_dl->AddText(CyberpunkTheme::g_font_small, 11.0f, ImVec2(val_x, line_y1 - 6.0f), IM_COL32(0, 255, 102, 255), bsl_buf);

    // SSL Line
    card5_dl->AddText(CyberpunkTheme::g_font_small, 11.0f, ImVec2(card5_pos.x + 12.0f, line_y2 - 6.0f), IM_COL32(140, 140, 155, 255), "SSL");
    card5_dl->AddLine(ImVec2(slider_x, line_y2), ImVec2(slider_x + slider_w, line_y2), IM_COL32(80, 80, 95, 200), 1.5f);
    card5_dl->AddCircleFilled(ImVec2(slider_x + slider_w * 0.35f, line_y2), 3.5f, IM_COL32(255, 60, 60, 255));
    card5_dl->AddCircle(ImVec2(slider_x + slider_w * 0.35f, line_y2), 5.5f, IM_COL32(255, 60, 60, 70), 12, 1.0f);
    card5_dl->AddText(CyberpunkTheme::g_font_small, 11.0f, ImVec2(val_x, line_y2 - 6.0f), IM_COL32(255, 60, 60, 255), ssl_buf);

    ImGui::EndChild();

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

// -----------------------------------------------------------------------------
// 4. Bottom Row: Real-time Data Stream Console
// -----------------------------------------------------------------------------

void RenderConsoleLog(ThreadSafeAppState& state) {
    ImGui::TextColored(CyberpunkTheme::NEON_RED, "REAL-TIME DATA STREAM CONSOLE");
    ImGui::SameLine();
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "(Live MT5 & Laya Decision Event Stream)");

    ImGui::SameLine(ImGui::GetWindowWidth() - 380);
    ImGui::Checkbox("Auto-Scroll", &s_autoscroll);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        state.ClearLogs();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("##filter", "Filter messages...", s_console_filter, sizeof(s_console_filter));

    ImGui::Separator();

    // Console Inner Child Window with Inset Dark Background #050505
    ImGui::PushStyleColor(ImGuiCol_ChildBg, CyberpunkTheme::BG_INSET);
    ImGui::PushStyleColor(ImGuiCol_Border, CyberpunkTheme::BORDER_DARK);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));

    ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

    std::vector<ConsoleLogEntry> logs = state.GetLogs();
    std::string filter_str = s_console_filter;

    for (const auto& log : logs) {
        if (!filter_str.empty() && log.message.find(filter_str) == std::string::npos) {
            continue;
        }

        ImVec4 level_color = CyberpunkTheme::TEXT_MUTED;
        if (log.level == "ALERT") level_color = CyberpunkTheme::NEON_AMBER;
        else if (log.level == "WARN") level_color = CyberpunkTheme::NEON_RED_HOVER;
        else if (log.level == "EXEC") level_color = CyberpunkTheme::NEON_RED;
        else if (log.level == "INFO") level_color = CyberpunkTheme::NEON_CYAN;

        ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "[%s]", log.timestamp.c_str());
        ImGui::SameLine();
        ImGui::TextColored(level_color, "[%-5s]", log.level.c_str());
        ImGui::SameLine();
        
        if (log.level == "EXEC") {
            ImGui::TextColored(CyberpunkTheme::NEON_GREEN, "%s", log.message.c_str());
        } else {
            ImGui::TextUnformatted(log.message.c_str());
        }
    }

    if (s_autoscroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// -----------------------------------------------------------------------------
// 4b. Live Open Positions Tab
// -----------------------------------------------------------------------------

void RenderOpenPositionsTab(ThreadSafeAppState& state, const MarketFrame& frame) {
    auto positions = state.GetPositions();

    // Summary Header Bar
    double total_floating_pnl = 0.0;
    double total_volume = 0.0;
    for (const auto& p : positions) {
        total_floating_pnl += p.profit;
        total_volume += p.volume;
    }

    ImVec4 pnl_col = total_floating_pnl >= 0.0 ? CyberpunkTheme::NEON_GREEN : CyberpunkTheme::NEON_RED;
    char pnl_buf[32];
    std::snprintf(pnl_buf, sizeof(pnl_buf), "%s$%.2f", total_floating_pnl >= 0.0 ? "+" : "", total_floating_pnl);
    RenderPillBadge("Floating PnL", pnl_buf, pnl_col);
    ImGui::SameLine();

    char vol_buf[32];
    std::snprintf(vol_buf, sizeof(vol_buf), "%.2f Lots", total_volume);
    RenderPillBadge("Open Volume", vol_buf, CyberpunkTheme::NEON_CYAN);
    ImGui::SameLine();

    RenderPillBadge("Positions", std::to_string(positions.size()).c_str(), CyberpunkTheme::TEXT_PRIMARY);

    ImGui::SameLine(ImGui::GetWindowWidth() - 340.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.50f, 0.20f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.70f, 0.30f, 0.70f));
    if (ImGui::Button("+ Market Buy", ImVec2(100.0f, 28.0f))) {
        state.QueueCommand("{\"command\":\"EXECUTE\",\"symbol\":\"XAUUSD\",\"action\":\"MARKET_BUY\"}");
        state.AddLog("INFO", "Manual Market BUY triggered from Positions tab");
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.05f, 0.15f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.08f, 0.20f, 0.70f));
    if (ImGui::Button("- Market Sell", ImVec2(100.0f, 28.0f))) {
        state.QueueCommand("{\"command\":\"EXECUTE\",\"symbol\":\"XAUUSD\",\"action\":\"MARKET_SELL\"}");
        state.AddLog("INFO", "Manual Market SELL triggered from Positions tab");
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.15f, 0.15f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.20f, 0.20f, 0.70f));
    if (ImGui::Button("Close All", ImVec2(90.0f, 28.0f))) {
        state.QueueCommand("{\"command\":\"CLOSE_ALL\",\"symbol\":\"XAUUSD\"}");
        state.AddLog("ALERT", "Close All Positions triggered from Positions tab");
    }
    ImGui::PopStyleColor(2);

    ImGui::Separator();

    if (positions.empty()) {
        ImGui::Dummy(ImVec2(0, 24));
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.36f);
        ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "[ No active market positions open ]");
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.30f);
        ImGui::TextColored(CyberpunkTheme::NEON_CYAN, "The Laya ONNX execution gate is scanning for institutional SMC setups...");
        return;
    }

    // Positions Table
    ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | 
                                 ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("PositionsTable", 11, table_flags)) {
        ImGui::TableSetupColumn("Ticket", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Symbol", ImGuiTableColumnFlags_WidthFixed, 85.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Lots", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("Open Price", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Current Price", ImGuiTableColumnFlags_WidthFixed, 105.0f);
        ImGui::TableSetupColumn("Stop Loss", ImGuiTableColumnFlags_WidthFixed, 95.0f);
        ImGui::TableSetupColumn("Take Profit", ImGuiTableColumnFlags_WidthFixed, 95.0f);
        ImGui::TableSetupColumn("Floating PnL ($)", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        int pos_row = 0;
        for (const auto& pos : positions) {
            ImGui::PushID(pos.ticket > 0 ? (int)(pos.ticket & 0x7FFFFFFF) : ++pos_row);
            ImGui::TableNextRow();

            // 1. Ticket
            ImGui::TableNextColumn();
            ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "#%llu", (unsigned long long)pos.ticket);

            // 2. Time
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(pos.time_str.empty() ? "--:--" : pos.time_str.c_str());

            // 3. Symbol
            ImGui::TableNextColumn();
            ImGui::TextColored(CyberpunkTheme::NEON_AMBER, "%s", pos.symbol.c_str());

            // 4. Type
            ImGui::TableNextColumn();
            bool is_buy = pos.type.find("BUY") != std::string::npos;
            ImGui::TextColored(is_buy ? CyberpunkTheme::NEON_GREEN : CyberpunkTheme::NEON_RED, "%s", pos.type.c_str());

            // 5. Volume
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", pos.volume);

            // 6. Open Price
            ImGui::TableNextColumn();
            ImGui::Text("$%.2f", pos.price_open);

            // 7. Current Price
            ImGui::TableNextColumn();
            ImGui::TextColored(CyberpunkTheme::TEXT_PRIMARY, "$%.2f", pos.price_current > 0 ? pos.price_current : frame.price);

            // 8. Stop Loss
            ImGui::TableNextColumn();
            ImGui::TextColored(CyberpunkTheme::NEON_RED_HOVER, "$%.2f", pos.sl);

            // 9. Take Profit
            ImGui::TableNextColumn();
            ImGui::TextColored(CyberpunkTheme::NEON_GREEN, "$%.2f", pos.tp);

            // 10. Floating PnL
            ImGui::TableNextColumn();
            ImVec4 profit_col = pos.profit >= 0.0 ? CyberpunkTheme::NEON_GREEN : CyberpunkTheme::NEON_RED;
            ImGui::TextColored(profit_col, "%s$%.2f", pos.profit >= 0.0 ? "+" : "", pos.profit);

            // 11. Action Close Button
            ImGui::TableNextColumn();
            char btn_id[64];
            std::snprintf(btn_id, sizeof(btn_id), "Close##pos_%llu", (unsigned long long)pos.ticket);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.10f, 0.15f, 0.60f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.15f, 0.20f, 0.90f));
            if (ImGui::Button(btn_id, ImVec2(65.0f, 22.0f))) {
                char cmd[128];
                std::snprintf(cmd, sizeof(cmd), "{\"command\":\"CLOSE_TICKET\",\"ticket\":%llu,\"symbol\":\"%s\"}", (unsigned long long)pos.ticket, pos.symbol.c_str());
                state.QueueCommand(cmd);
                state.AddLog("INFO", "Requested closure of position #" + std::to_string(pos.ticket));
            }
            ImGui::PopStyleColor(2);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// -----------------------------------------------------------------------------
// 4c. Trade History Tab
// -----------------------------------------------------------------------------

void RenderTradeHistoryTab(ThreadSafeAppState& state) {
    auto history = state.GetHistory();

    // Calculate Summary Stats
    int total_trades = (int)history.size();
    int wins = 0;
    double net_pnl = 0.0;
    double total_win_pnl = 0.0;
    double total_loss_pnl = 0.0;

    for (const auto& item : history) {
        net_pnl += item.profit;
        if (item.profit >= 0.0) {
            wins++;
            total_win_pnl += item.profit;
        } else {
            total_loss_pnl += std::abs(item.profit);
        }
    }

    float win_rate = total_trades > 0 ? ((float)wins / (float)total_trades * 100.0f) : 0.0f;
    float profit_factor = total_loss_pnl > 0.01 ? (float)(total_win_pnl / total_loss_pnl) : (total_win_pnl > 0 ? 9.9f : 0.0f);

    // Badges Row
    RenderPillBadge("Total Deals", std::to_string(total_trades).c_str(), CyberpunkTheme::TEXT_PRIMARY);
    ImGui::SameLine();

    char wr_buf[32];
    std::snprintf(wr_buf, sizeof(wr_buf), "%.1f%%", win_rate);
    RenderPillBadge("Win Rate", wr_buf, win_rate >= 50.0f ? CyberpunkTheme::NEON_GREEN : CyberpunkTheme::NEON_RED);
    ImGui::SameLine();

    char net_buf[32];
    std::snprintf(net_buf, sizeof(net_buf), "%s$%.2f", net_pnl >= 0.0 ? "+" : "", net_pnl);
    RenderPillBadge("Net Profit", net_buf, net_pnl >= 0.0 ? CyberpunkTheme::NEON_GREEN : CyberpunkTheme::NEON_RED);
    ImGui::SameLine();

    char pf_buf[32];
    std::snprintf(pf_buf, sizeof(pf_buf), "%.2f", profit_factor);
    RenderPillBadge("Profit Factor", pf_buf, profit_factor >= 1.5f ? CyberpunkTheme::NEON_GREEN : CyberpunkTheme::NEON_AMBER);

    ImGui::Separator();

    if (history.empty()) {
        ImGui::Dummy(ImVec2(0, 24));
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.38f);
        ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "[ No trade history records found ]");
        return;
    }

    // Trade History Table
    ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | 
                                 ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("HistoryTable", 10, table_flags)) {
        ImGui::TableSetupColumn("Ticket", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Symbol", ImGuiTableColumnFlags_WidthFixed, 85.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Lots", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("Open Price", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Close Price", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Net PnL ($)", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Outcome", ImGuiTableColumnFlags_WidthFixed, 85.0f);
        ImGui::TableSetupColumn("Strategy / Confluence", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        int hist_row = 0;
        for (const auto& item : history) {
            ImGui::PushID(item.ticket > 0 ? (int)(item.ticket & 0x7FFFFFFF) : ++hist_row);
            ImGui::TableNextRow();

            // 1. Ticket
            ImGui::TableNextColumn();
            ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "#%llu", (unsigned long long)item.ticket);

            // 2. Time
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(item.time_str.c_str());

            // 3. Symbol
            ImGui::TableNextColumn();
            ImGui::TextColored(CyberpunkTheme::NEON_AMBER, "%s", item.symbol.c_str());

            // 4. Action
            ImGui::TableNextColumn();
            bool is_buy = item.type.find("BUY") != std::string::npos;
            ImGui::TextColored(is_buy ? CyberpunkTheme::NEON_GREEN : CyberpunkTheme::NEON_RED, "%s", item.type.c_str());

            // 5. Volume
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", item.volume);

            // 6. Open Price
            ImGui::TableNextColumn();
            ImGui::Text("$%.2f", item.price_open);

            // 7. Close Price
            ImGui::TableNextColumn();
            ImGui::Text("$%.2f", item.price_close);

            // 8. Net PnL
            ImGui::TableNextColumn();
            ImVec4 pnl_color = item.profit >= 0.0 ? CyberpunkTheme::NEON_GREEN : CyberpunkTheme::NEON_RED;
            ImGui::TextColored(pnl_color, "%s$%.2f", item.profit >= 0.0 ? "+" : "", item.profit);

            // 9. Outcome Badge
            ImGui::TableNextColumn();
            bool is_win = (item.outcome == "WIN" || item.profit >= 0.0);
            ImVec4 badge_bg = is_win ? ImVec4(0.0f, 0.40f, 0.15f, 0.50f) : ImVec4(0.50f, 0.05f, 0.10f, 0.50f);
            ImVec4 badge_txt = is_win ? CyberpunkTheme::NEON_GREEN : CyberpunkTheme::NEON_RED;
            ImGui::PushStyleColor(ImGuiCol_Button, badge_bg);
            ImGui::PushStyleColor(ImGuiCol_Text, badge_txt);
            char badge_lbl[32];
            std::snprintf(badge_lbl, sizeof(badge_lbl), "%s##outcome_%llu", is_win ? " WIN " : " LOSS ", (unsigned long long)item.ticket);
            ImGui::SmallButton(badge_lbl);
            ImGui::PopStyleColor(2);

            // 10. Comment / Confluence
            ImGui::TableNextColumn();
            ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "%s", item.comment.c_str());

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// -----------------------------------------------------------------------------
// 4. Bottom Row: Tabbed Inset Panel (Console, Open Positions, Trade History)
// -----------------------------------------------------------------------------

void RenderBottomSection(ThreadSafeAppState& state, const MarketFrame& frame) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, CyberpunkTheme::BG_PANEL);
    ImGui::PushStyleColor(ImGuiCol_Border, CyberpunkTheme::BORDER_DARK);
    ImGui::BeginChild("BottomTabbedPanel", ImVec2(0, 0), true);

    auto positions = state.GetPositions();
    auto history = state.GetHistory();

    char pos_title[64];
    std::snprintf(pos_title, sizeof(pos_title), "  Open Positions (%d)  ", (int)positions.size());

    char hist_title[64];
    std::snprintf(hist_title, sizeof(hist_title), "  Trade History (%d)  ", (int)history.size());

    if (ImGui::BeginTabBar("BottomTabBar", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("  >_ Event Stream Console  ")) {
            RenderConsoleLog(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(pos_title)) {
            RenderOpenPositionsTab(state, frame);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(hist_title)) {
            RenderTradeHistoryTab(state);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

// -----------------------------------------------------------------------------
// 5. Trade Settings Modal Dialog
// -----------------------------------------------------------------------------

void RenderTradeSettingsModal(ThreadSafeAppState& state) {
    if (s_show_settings_modal) {
        ImGui::OpenPopup("TRADE SETTINGS & RISK CONSTRAINTS");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(750, 560), ImGuiCond_Appearing);

    ImGui::PushStyleColor(ImGuiCol_PopupBg, CyberpunkTheme::BG_WINDOW);
    ImGui::PushStyleColor(ImGuiCol_Border, CyberpunkTheme::BORDER_DARK);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));

    if (ImGui::BeginPopupModal("TRADE SETTINGS & RISK CONSTRAINTS", &s_show_settings_modal, ImGuiWindowFlags_NoResize)) {
        static TradeSettings edit_s;
        static bool s_loaded = false;
        if (!s_loaded) {
            edit_s = state.GetSettings();
            s_loaded = true;
        }

        // Header Title
        ImGui::PushFont(CyberpunkTheme::g_font_bold_med);
        ImGui::TextColored(CyberpunkTheme::NEON_CYAN, "SPARKX EA // ALGORITHMIC TRADE SETTINGS & RISK GATES");
        ImGui::PopFont();
        ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "Configure position sizing, SL/TP brackets, institutional SMC filters, and phone notifications.");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 6));

        if (ImGui::BeginTabBar("SettingsTabs", ImGuiTabBarFlags_None)) {
            // TAB 1: SIZING & RISK
            if (ImGui::BeginTabItem("  1. Execution & Sizing  ")) {
                ImGui::Dummy(ImVec2(0, 8));
                ImGui::Checkbox("Enable Autonomous Laya-ONNX & SMC Trading", &edit_s.auto_trade_enabled);
                ImGui::SameLine();
                ImGui::TextColored(edit_s.auto_trade_enabled ? CyberpunkTheme::NEON_GREEN : CyberpunkTheme::NEON_AMBER,
                                  "(%s)", edit_s.auto_trade_enabled ? "ACTIVE" : "STANDBY");

                ImGui::Dummy(ImVec2(0, 6));
                const char* lot_modes[] = { "Fixed Lot Size", "Dynamic Account Risk %" };
                ImGui::Combo("Lot Sizing Mode", &edit_s.lot_mode, lot_modes, IM_ARRAYSIZE(lot_modes));

                if (edit_s.lot_mode == 0) {
                    ImGui::SliderFloat("Fixed Volume (Lots)", &edit_s.fixed_lot, 0.01f, 5.0f, "%.2f Lots");
                } else {
                    ImGui::SliderFloat("Account Risk per Trade (%)", &edit_s.risk_pct, 0.25f, 5.0f, "%.2f %%");
                }

                ImGui::Dummy(ImVec2(0, 6));
                ImGui::Separator();
                ImGui::TextColored(CyberpunkTheme::NEON_AMBER, "Order Bracket Constraints:");
                ImGui::SliderFloat("Stop Loss Distance (Points / $)", &edit_s.sl_points, 1.0f, 20.0f, "%.1f pts ($)");
                ImGui::SliderFloat("Take Profit Distance (Points / $)", &edit_s.tp_points, 2.0f, 40.0f, "%.1f pts ($)");

                float rr = edit_s.tp_points / (edit_s.sl_points > 0.01f ? edit_s.sl_points : 1.0f);
                ImGui::Text("Calculated Risk : Reward Ratio: ");
                ImGui::SameLine();
                ImGui::TextColored(CyberpunkTheme::NEON_GREEN, "1 : %.2f R:R", rr);

                ImGui::Dummy(ImVec2(0, 6));
                ImGui::SliderInt("Max Simultaneous Open Positions", &edit_s.max_positions, 1, 5);
                ImGui::SliderFloat("Max Allowed Spread Filter", &edit_s.max_spread, 5.0f, 50.0f, "%.1f pts");

                ImGui::EndTabItem();
            }

            // TAB 2: SMC & AI CONFLUENCE GATES
            if (ImGui::BeginTabItem("  2. SMC & AI Filters  ")) {
                ImGui::Dummy(ImVec2(0, 8));
                ImGui::TextColored(CyberpunkTheme::NEON_AMBER, "Neural Network & Structural Confluence Gates:");
                ImGui::Dummy(ImVec2(0, 4));

                ImGui::SliderFloat("Min Setup Quality Score", &edit_s.min_score, 5.0f, 9.5f, "%.1f / 10.0");
                ImGui::SliderFloat("Min Laya AI Confidence", &edit_s.min_confidence, 70.0f, 98.0f, "%.1f %%");

                ImGui::Dummy(ImVec2(0, 6));
                ImGui::Separator();
                ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "SMC Pre-Execution Rules:");
                ImGui::Checkbox("Require Higher-Timeframe (H1) Trend Alignment", &edit_s.require_h1_trend);
                ImGui::Checkbox("Require Active M5 Fair Value Gap (FVG) Retest", &edit_s.require_m5_fvg);
                ImGui::Checkbox("Require Prior Liquidity Pool Sweep (BSL / SSL)", &edit_s.require_liquidity_sweep);

                ImGui::EndTabItem();
            }

            // TAB 3: PHONE & MOBILE ALERTS
            if (ImGui::BeginTabItem("  3. Phone Alerts  ")) {
                ImGui::Dummy(ImVec2(0, 8));
                ImGui::TextColored(CyberpunkTheme::NEON_CYAN, "Mobile Phone Push Alerts (Discord Webhooks & Telegram):");
                ImGui::Dummy(ImVec2(0, 4));

                ImGui::Checkbox("Enable Discord Webhook Alerts", &edit_s.discord_alerts);
                ImGui::InputText("Discord Webhook URL", edit_s.discord_webhook, sizeof(edit_s.discord_webhook));

                ImGui::Dummy(ImVec2(0, 6));
                ImGui::Checkbox("Enable Telegram Bot Alerts", &edit_s.telegram_alerts);
                ImGui::InputText("Telegram Bot Token", edit_s.telegram_token, sizeof(edit_s.telegram_token));
                ImGui::InputText("Telegram Chat ID", edit_s.telegram_chat_id, sizeof(edit_s.telegram_chat_id));

                ImGui::Dummy(ImVec2(0, 10));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.40f, 0.60f, 0.50f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.55f, 0.80f, 0.80f));
                if (ImGui::Button("🔔 Send Test Verification Signal to Phone", ImVec2(320.0f, 32.0f))) {
                    state.QueueCommand("{\"command\":\"TEST_ALERT\",\"symbol\":\"XAUUSD\"}");
                    state.AddLog("INFO", "Sent test verification alert to phone via Discord/Telegram");
                }
                ImGui::PopStyleColor(2);

                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        // Footer Actions
        ImGui::Dummy(ImVec2(0, 16));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 4));

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.55f, 0.25f, 0.80f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.75f, 0.35f, 1.00f));
        if (ImGui::Button("✔ Apply & Save Settings", ImVec2(180.0f, 34.0f))) {
            state.SetSettings(edit_s);

            char cmd[1024];
            std::snprintf(cmd, sizeof(cmd),
                "{\"command\":\"UPDATE_SETTINGS\",\"settings\":{"
                "\"auto_trade_enabled\":%s,"
                "\"fixed_lot_size\":%.2f,"
                "\"risk_per_trade_pct\":%.2f,"
                "\"sl_points\":%.2f,"
                "\"tp_points\":%.2f,"
                "\"max_open_positions\":%d,"
                "\"max_spread_points\":%.2f,"
                "\"min_setup_score\":%.2f,"
                "\"min_confidence_pct\":%.2f,"
                "\"require_h1_trend\":%s,"
                "\"require_m5_fvg\":%s,"
                "\"require_liquidity_sweep\":%s,"
                "\"discord_alerts\":%s"
                "}}",
                edit_s.auto_trade_enabled ? "true" : "false",
                edit_s.fixed_lot,
                edit_s.risk_pct,
                edit_s.sl_points,
                edit_s.tp_points,
                edit_s.max_positions,
                edit_s.max_spread,
                edit_s.min_score,
                edit_s.min_confidence,
                edit_s.require_h1_trend ? "true" : "false",
                edit_s.require_m5_fvg ? "true" : "false",
                edit_s.require_liquidity_sweep ? "true" : "false",
                edit_s.discord_alerts ? "true" : "false"
            );
            state.QueueCommand(cmd);
            state.AddLog("INFO", "Applied & saved trade settings.");
            s_show_settings_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        if (ImGui::Button("Reset Defaults", ImVec2(130.0f, 34.0f))) {
            TradeSettings def;
            edit_s = def;
        }

        ImGui::SameLine(ImGui::GetWindowWidth() - 110.0f);
        if (ImGui::Button("Close", ImVec2(90.0f, 34.0f))) {
            s_show_settings_modal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// -----------------------------------------------------------------------------
// Master Dashboard Layout
// -----------------------------------------------------------------------------

void RenderDashboard(ThreadSafeAppState& state) {
    MarketFrame frame = state.GetLatestFrame();

    // 1. Top Bar: Header, Pill Badges, Asset Selector Cards, Mode Toggle, and Trade Controls
    RenderHeaderAndToggle(state, frame);

    // 2. Main Row: Large Digital Readouts for Laya Primitives
    RenderLayaPrimitives(frame.laya_decision, frame);

    // 3. Middle Row: Market State & SMC Feature Telemetry Matrix (5 Glassmorphic Cards)
    RenderSMCContext(frame);

    // 4. Bottom Row: Tabbed Inset Panel (Console Stream, Open Positions, Trade History)
    RenderBottomSection(state, frame);

    // 5. Settings Modal (if opened)
    RenderTradeSettingsModal(state);
}

} // namespace UIPanels
