#pragma once

#include "app_state.hpp"

namespace UIPanels {
    // Render master cyberpunk dashboard layout
    void RenderDashboard(ThreadSafeAppState& state);

    // Individual sub-panels
    void RenderHeaderAndToggle(ThreadSafeAppState& state, const MarketFrame& frame);
    void RenderLayaPrimitives(const LayaOutput& laya, const MarketFrame& frame);
    void RenderSMCContext(const MarketFrame& frame);
    void RenderConsoleLog(ThreadSafeAppState& state);
}
