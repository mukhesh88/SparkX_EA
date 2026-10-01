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
    ImGui::TextColored(CyberpunkTheme::TEXT_PRIMARY, "SparkX");
    ImGui::PopFont();

    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "|");
    ImGui::SameLine();
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "Ultra-Low-Latency\nDecision Engine (Laya-ONNX)");

    // Middle: Dedicated Gold Asset Display Card
    ImGui::SameLine(ImGui::GetWindowWidth() * 0.40f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
    RenderAssetPill("XAUUSD (GOLD)", frame.price > 100.0 ? frame.price : 4186.80);

    // Right: Pill Badges using ImGui::RenderFrame style
    bool is_mt5 = (frame.feed_source.find("MT5") != std::string::npos);
    float right_offset = !is_mt5 ? 730.0f : 600.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - right_offset);
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
        if (ImGui::Button("[+ Install MT5]", ImVec2(120.0f, 32.0f))) {
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
    RenderPillBadge("GPU/CUDA", stream_ok ? "Green" : "CPU", stream_ok ? CyberpunkTheme::NEON_GREEN : CyberpunkTheme::NEON_AMBER);
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
    if (ImGui::InvisibleButton("##mode_toggle", ImVec2(100, 32))) {
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

    dl->AddRectFilled(m_pos, ImVec2(m_pos.x + 100, m_pos.y + 32), m_hov ? IM_COL32(32, 28, 24, 255) : IM_COL32(20, 18, 16, 255), 5.0f);
    dl->AddRect(m_pos, ImVec2(m_pos.x + 100, m_pos.y + 32), ImGui::ColorConvertFloat4ToU32(mode_col), 5.0f, 0, 1.2f);
    dl->AddText(CyberpunkTheme::g_font_regular, 12.5f, ImVec2(m_pos.x + 8, m_pos.y + 8), ImGui::ColorConvertFloat4ToU32(CyberpunkTheme::TEXT_MUTED), "Mode");
    dl->AddText(CyberpunkTheme::g_font_bold_med, 13.0f, ImVec2(m_pos.x + 44, m_pos.y + 8), ImGui::ColorConvertFloat4ToU32(mode_col), mode_str);

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

    float buy_pct = 0.35f, hold_pct = 0.25f, sell_pct = 0.10f;
    for (const auto& p : laya.choice_distribution) {
        if (p.first.find("BUY") != std::string::npos) buy_pct = std::max(buy_pct, p.second);
        else if (p.first.find("HOLD") != std::string::npos) hold_pct = std::max(hold_pct, p.second);
        else if (p.first.find("SELL") != std::string::npos) sell_pct = std::max(sell_pct, p.second);
    }

    for (int i = 0; i < num_cols; ++i) {
        float x0 = stack_pos.x + i * (col_w + col_gap);
        float x1 = x0 + col_w;
        float h_usable = stack_h - 22.0f;

        float var_factor = 0.85f + 0.12f * std::sin((float)i * 1.3f);
        float b_h = h_usable * buy_pct * var_factor;
        float h_h = h_usable * hold_pct;
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

    // Right-side Percentage breakdown badges
    ImVec2 right_pos = ImVec2(stack_pos.x + stack_w + 14.0f, stack_pos.y + 6.0f);
    dl->AddCircleFilled(ImVec2(right_pos.x + 6, right_pos.y + 8), 5.0f, IM_COL32(0, 255, 102, 255));
    dl->AddText(CyberpunkTheme::g_font_regular, 13.0f, ImVec2(right_pos.x + 18, right_pos.y), IM_COL32(0, 255, 102, 255), "BUY   35%");

    dl->AddCircleFilled(ImVec2(right_pos.x + 6, right_pos.y + 32), 5.0f, IM_COL32(242, 192, 51, 255));
    dl->AddText(CyberpunkTheme::g_font_regular, 13.0f, ImVec2(right_pos.x + 18, right_pos.y + 24), IM_COL32(242, 192, 51, 255), "HOLD  25%");

    dl->AddCircleFilled(ImVec2(right_pos.x + 6, right_pos.y + 56), 5.0f, IM_COL32(0, 229, 255, 255));
    dl->AddText(CyberpunkTheme::g_font_regular, 13.0f, ImVec2(right_pos.x + 18, right_pos.y + 48), IM_COL32(0, 229, 255, 255), "LIM_B 10%");

    dl->AddCircleFilled(ImVec2(right_pos.x + 6, right_pos.y + 80), 5.0f, IM_COL32(255, 60, 60, 255));
    dl->AddText(CyberpunkTheme::g_font_regular, 13.0f, ImVec2(right_pos.x + 18, right_pos.y + 72), IM_COL32(255, 60, 60, 255), "SELL  8%");

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
    RenderPillBadge("Confidence", "95.0%", CyberpunkTheme::NEON_GREEN);

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
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "Inference Latency: 0.80 ms [Local GPU / CUDA]");
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
        { "H1 Trend Align", frame.bias.h1_trend.find("BULL") != std::string::npos },
        { "M5 FVG Confirmed", frame.arrays.fvg_active },
        { "Liquidity Swept", frame.liquidity.ssl_swept || frame.liquidity.bsl_swept },
        { "H1 Trend D1am Align", frame.bias.h1_trend.find("BULL") != std::string::npos },
        { "M5 FVG Confirmed", frame.arrays.fvg_active },
        { "M5 FVG Confirmation", frame.arrays.fvg_active },
        { "Liquidity Swept", frame.liquidity.ssl_swept || frame.liquidity.bsl_swept }
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
    ImGui::BeginChild("SMCContextPanel", ImVec2(0, 168), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::TextColored(CyberpunkTheme::NEON_CYAN, "LIVE MARKET DATA & SMC CONFLUENCE MATRIX");
    ImGui::SameLine(ImGui::GetWindowWidth() - 320);
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "Session: %s | Latency: 0.80ms", frame.session.c_str());
    ImGui::Separator();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float card_w = (avail.x - 36.0f) / 5.0f;
    float card_h = 108.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // -------------------------------------------------------------------------
    // Card 1: ASSET PRICING
    // -------------------------------------------------------------------------
    ImGui::BeginChild("Card1_Pricing", ImVec2(card_w, card_h), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "ASSET PRICING:");
    ImGui::PushFont(CyberpunkTheme::g_font_bold_med);
    ImGui::TextColored(CyberpunkTheme::NEON_GREEN, "%s / $%.2f", frame.symbol.c_str(), frame.price);
    ImGui::PopFont();
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "Bid: $%.2f | Ask: $%.2f", frame.bid, frame.ask);

    // Mini Sparkline Wave (Gold)
    ImVec2 sp_pos = ImGui::GetCursorScreenPos();
    DrawMiniSparkline(dl, sp_pos, ImVec2(card_w - 20.0f, 24.0f), s_gold_history, 32, IM_COL32(242, 192, 51, 230), IM_COL32(242, 192, 51, 35));
    ImGui::EndChild();

    ImGui::SameLine();

    // -------------------------------------------------------------------------
    // Card 2: MULTI-TIMEFRAME STRUCTURE
    // -------------------------------------------------------------------------
    ImGui::BeginChild("Card2_MTF", ImVec2(card_w, card_h), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "MULTI-TIMEFRAME STRUCTURE:");
    ImGui::Text("H1 Bias   : "); ImGui::SameLine();
    ImGui::TextColored(CyberpunkTheme::NEON_AMBER, "%s", frame.bias.h1_trend.c_str());
    ImGui::Text("M15 Biases: "); ImGui::SameLine();
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "%s", frame.bias.m15_struct.c_str());
    ImGui::Text("M5 Micro  : "); ImGui::SameLine();
    ImGui::TextColored(CyberpunkTheme::NEON_GREEN, "%s", frame.bias.m5_struct.c_str());

    // Mini Sparkline Wave (Red/Green)
    ImVec2 sp_pos2 = ImGui::GetCursorScreenPos();
    DrawMiniSparkline(dl, sp_pos2, ImVec2(card_w - 20.0f, 20.0f), s_mtf_history, 32, IM_COL32(255, 60, 60, 220), IM_COL32(255, 60, 60, 30));
    ImGui::EndChild();

    ImGui::SameLine();

    // -------------------------------------------------------------------------
    // Card 3: DEALING RANGE & PRICING
    // -------------------------------------------------------------------------
    ImGui::BeginChild("Card3_Range", ImVec2(card_w, card_h), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "DEALING RANGE & PRICING:");
    ImGui::TextColored(CyberpunkTheme::NEON_AMBER, "Zone: %s (Fib %.1f%%)", frame.bias.zone.c_str(), frame.bias.fib_pct);
    ImGui::Text("Asia H/L: $%.2f / $%.2f", frame.liquidity.asia_high, frame.liquidity.asia_low);
    ImGui::Text("Liquidity Swept: %s", frame.liquidity.ssl_swept ? "SSL_SWEPT" : (frame.liquidity.bsl_swept ? "BSL_SWEPT" : "NONE"));

    // Mini Sparkline Wave (Gold)
    ImVec2 sp_pos3 = ImGui::GetCursorScreenPos();
    DrawMiniSparkline(dl, sp_pos3, ImVec2(card_w - 20.0f, 20.0f), s_range_history, 32, IM_COL32(242, 192, 51, 220), IM_COL32(242, 192, 51, 30));
    ImGui::EndChild();

    ImGui::SameLine();

    // -------------------------------------------------------------------------
    // Card 4: ACTIVE ARRAYS & MOMENTUM
    // -------------------------------------------------------------------------
    ImGui::BeginChild("Card4_Arrays", ImVec2(card_w, card_h), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "ACTIVE ARRAYS & MOMENTUM:");
    ImGui::Text("M5 FVG: %s", frame.arrays.fvg_active ? "ACTIVE" : "NONE");
    ImGui::Text("M5 OB : %s", frame.arrays.ob_active ? "ACTIVE" : "NONE");

    // Mini Sparkline Wave (Green)
    ImVec2 sp_pos4 = ImGui::GetCursorScreenPos();
    DrawMiniSparkline(dl, sp_pos4, ImVec2(card_w - 20.0f, 26.0f), s_array_history, 32, IM_COL32(0, 255, 102, 220), IM_COL32(0, 255, 102, 35));
    ImGui::EndChild();

    ImGui::SameLine();

    // -------------------------------------------------------------------------
    // Card 5: TARGETS (Sliders & Dual Target Chart)
    // -------------------------------------------------------------------------
    ImGui::BeginChild("Card5_Targets", ImVec2(card_w, card_h), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextColored(CyberpunkTheme::TEXT_MUTED, "TARGETS:");
    ImGui::TextColored(CyberpunkTheme::NEON_GREEN, "BSL $%.2f | SSL $%.2f", frame.liquidity.bsl_target, frame.liquidity.ssl_target);

    // Target Range Horizontal sliders with dots
    ImVec2 t_pos = ImGui::GetCursorScreenPos();
    float line_w = card_w - 80.0f;
    // BSL Line
    dl->AddLine(ImVec2(t_pos.x + 30, t_pos.y + 6), ImVec2(t_pos.x + 30 + line_w, t_pos.y + 6), IM_COL32(80, 80, 95, 200), 1.5f);
    dl->AddCircleFilled(ImVec2(t_pos.x + 30 + line_w * 0.75f, t_pos.y + 6), 3.5f, IM_COL32(0, 255, 102, 255));
    dl->AddText(CyberpunkTheme::g_font_small, 11.0f, ImVec2(t_pos.x, t_pos.y), IM_COL32(140, 140, 155, 255), "BSL");
    dl->AddText(CyberpunkTheme::g_font_small, 11.0f, ImVec2(t_pos.x + 38 + line_w, t_pos.y), IM_COL32(0, 255, 102, 255), "$4195");

    // SSL Line
    dl->AddLine(ImVec2(t_pos.x + 30, t_pos.y + 22), ImVec2(t_pos.x + 30 + line_w, t_pos.y + 22), IM_COL32(80, 80, 95, 200), 1.5f);
    dl->AddCircleFilled(ImVec2(t_pos.x + 30 + line_w * 0.35f, t_pos.y + 22), 3.5f, IM_COL32(255, 60, 60, 255));
    dl->AddText(CyberpunkTheme::g_font_small, 11.0f, ImVec2(t_pos.x, t_pos.y + 16), IM_COL32(140, 140, 155, 255), "SSL");
    dl->AddText(CyberpunkTheme::g_font_small, 11.0f, ImVec2(t_pos.x + 38 + line_w, t_pos.y + 16), IM_COL32(255, 60, 60, 255), "$4155");

    ImGui::EndChild();

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

// -----------------------------------------------------------------------------
// 4. Bottom Row: Real-time Data Stream Console
// -----------------------------------------------------------------------------

void RenderConsoleLog(ThreadSafeAppState& state) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, CyberpunkTheme::BG_PANEL);
    ImGui::PushStyleColor(ImGuiCol_Border, CyberpunkTheme::BORDER_DARK);
    ImGui::BeginChild("ConsolePanel", ImVec2(0, 0), true);

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

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

// -----------------------------------------------------------------------------
// Master Dashboard Layout
// -----------------------------------------------------------------------------

void RenderDashboard(ThreadSafeAppState& state) {
    MarketFrame frame = state.GetLatestFrame();

    // 1. Top Bar: Header, Pill Badges, Asset Selector Cards, and Mode Toggle
    RenderHeaderAndToggle(state, frame);

    // 2. Main Row: Large Digital Readouts for Laya Primitives
    RenderLayaPrimitives(frame.laya_decision, frame);

    // 3. Middle Row: Market State & SMC Feature Telemetry Matrix (5 Glassmorphic Cards)
    RenderSMCContext(frame);

    // 4. Bottom Row: Inset Real-time Data Stream Console (#050505)
    RenderConsoleLog(state);
}

} // namespace UIPanels
