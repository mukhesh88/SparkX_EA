#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <deque>
#include <map>

enum class EngineState {
    STANDBY = 0,    // Idle / Observing market
    ARMED = 1,      // Actively evaluating and executing trades
    EMERGENCY_KILL = 2 // Hard kill-switch: Cancel all orders & halt inference
};

struct SMCBias {
    std::string h1_trend = "RANGE";
    std::string m15_struct = "NEUTRAL";
    std::string m5_struct = "NEUTRAL";
    std::string zone = "EQUILIBRIUM";
    float fib_pct = 50.0f;
};

struct SMCLiquidity {
    double asia_high = 0.0;
    double asia_low = 0.0;
    bool bsl_swept = false;
    bool ssl_swept = false;
    double bsl_target = 0.0;
    double ssl_target = 0.0;
};

struct SMCArrays {
    bool fvg_active = false;
    std::string fvg_direction = "NONE";
    bool ob_active = false;
    std::string ob_direction = "NONE";
};

struct NoulPrimitive {
    std::string hypothesis;
    float prob_true = 0.5f;
    float prob_false = 0.5f;
    bool is_validated = false;
};

struct LayaOutput {
    // Choice primitive
    std::string choice_action = "HOLD";
    float choice_confidence = 0.95f;
    std::vector<std::pair<std::string, float>> choice_distribution;

    // Score primitive
    float score_grade = 1.0f; // 1.0 to 10.0
    int score_top_grade = 1;
    std::vector<float> score_distribution;

    // Noul primitives
    std::vector<NoulPrimitive> noul_checks;

    // Telemetry
    float inference_latency_ms = 0.0f;
    bool gpu_accelerated = true;
    std::string execution_provider = "CUDA_Execution_Provider";
};

struct MarketFrame {
    std::string symbol = "XAUUSD";
    std::string timestamp = "00:00:00 UTC";
    uint64_t epoch_ms = 0;
    double price = 2365.40;
    double bid = 2365.20;
    double ask = 2365.60;
    float spread = 1.2f;
    std::string session = "NY_OVERLAP";
    int token_count = 124;
    std::string compressed_state;

    SMCBias bias;
    SMCLiquidity liquidity;
    SMCArrays arrays;
    
    LayaOutput laya_decision;
    float ingest_latency_ms = 0.0f;
    bool is_fresh = false;
    bool is_live_feed = false;
    std::string feed_source = "SYNTHETIC";
};

struct ConsoleLogEntry {
    std::string timestamp;
    std::string level; // "INFO", "WARN", "ALERT", "EXEC"
    std::string message;
};

struct TradePosition {
    uint64_t ticket = 0;
    uint64_t time = 0;
    std::string time_str;
    std::string symbol = "XAUUSD";
    std::string type = "BUY"; // "BUY" or "SELL"
    double volume = 0.10;
    double price_open = 0.0;
    double price_current = 0.0;
    double sl = 0.0;
    double tp = 0.0;
    double profit = 0.0;
    std::string comment;
};

struct TradeHistoryItem {
    uint64_t ticket = 0;
    uint64_t time = 0;
    std::string time_str;
    std::string symbol = "XAUUSD";
    std::string type = "BUY"; // "BUY" or "SELL"
    double volume = 0.10;
    double price_open = 0.0;
    double price_close = 0.0;
    double profit = 0.0;
    std::string outcome = "WIN"; // "WIN" or "LOSS"
    std::string comment;
};

struct TradeSettings {
    bool auto_trade_enabled = true;
    int lot_mode = 0; // 0 = Fixed Lot, 1 = Dynamic Risk %
    float fixed_lot = 0.10f;
    float risk_pct = 1.0f;
    float sl_points = 4.5f;
    float tp_points = 12.0f;
    int max_positions = 2;
    float max_spread = 25.0f;
    float min_score = 7.0f;
    float min_confidence = 85.0f;
    bool require_h1_trend = true;
    bool require_m5_fvg = true;
    bool require_liquidity_sweep = false;
    bool discord_alerts = false;
    bool telegram_alerts = false;
    char discord_webhook[512] = "";
    char telegram_token[128] = "";
    char telegram_chat_id[64] = "";
};

class ThreadSafeAppState {
public:
    ThreadSafeAppState() = default;

    void SetEngineState(EngineState state) {
        engine_state_.store(state, std::memory_order_relaxed);
    }

    EngineState GetEngineState() const {
        return engine_state_.load(std::memory_order_relaxed);
    }

    void SetActiveSymbol(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        active_symbol_ = symbol;
    }

    std::string GetActiveSymbol() const {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        return active_symbol_;
    }

    std::vector<std::string> GetAvailableSymbols() const {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        std::vector<std::string> syms;
        for (const auto& kv : frames_) {
            syms.push_back(kv.first);
        }
        return syms;
    }

    void UpdateMarketFrame(const MarketFrame& frame) {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        frames_[frame.symbol] = frame;
        frames_[frame.symbol].is_fresh = true;
        received_frames_count_++;
    }

    MarketFrame GetLatestFrame() {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        auto it = frames_.find(active_symbol_);
        if (it != frames_.end()) {
            MarketFrame copy = it->second;
            it->second.is_fresh = false;
            return copy;
        }
        if (!frames_.empty()) {
            MarketFrame copy = frames_.begin()->second;
            return copy;
        }
        MarketFrame default_frame;
        default_frame.symbol = active_symbol_;
        return default_frame;
    }

    void AddLog(const std::string& level, const std::string& message) {
        std::lock_guard<std::mutex> lock(log_mutex_);
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        struct tm timeinfo;
#if defined(_WIN32)
        localtime_s(&timeinfo, &time_t_now);
#else
        localtime_r(&time_t_now, &timeinfo);
#endif
        strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);

        logs_.push_back({std::string(buf), level, message});
        if (logs_.size() > 500) {
            logs_.pop_front();
        }
    }

    std::vector<ConsoleLogEntry> GetLogs() {
        std::lock_guard<std::mutex> lock(log_mutex_);
        return std::vector<ConsoleLogEntry>(logs_.begin(), logs_.end());
    }

    void ClearLogs() {
        std::lock_guard<std::mutex> lock(log_mutex_);
        logs_.clear();
    }

    uint64_t GetFrameCount() const {
        return received_frames_count_.load(std::memory_order_relaxed);
    }

    void SetZmqConnected(bool connected) {
        zmq_connected_.store(connected, std::memory_order_relaxed);
    }

    bool IsZmqConnected() const {
        return zmq_connected_.load(std::memory_order_relaxed);
    }

    // Trade Positions Management
    void UpdatePositions(const std::vector<TradePosition>& pos) {
        std::lock_guard<std::mutex> lock(trade_mutex_);
        positions_ = pos;
    }

    std::vector<TradePosition> GetPositions() const {
        std::lock_guard<std::mutex> lock(trade_mutex_);
        return positions_;
    }

    // Trade History Management
    void UpdateHistory(const std::vector<TradeHistoryItem>& hist) {
        std::lock_guard<std::mutex> lock(trade_mutex_);
        history_ = hist;
    }

    std::vector<TradeHistoryItem> GetHistory() const {
        std::lock_guard<std::mutex> lock(trade_mutex_);
        return history_;
    }

    // Trade Settings Management
    void SetSettings(const TradeSettings& settings) {
        std::lock_guard<std::mutex> lock(trade_mutex_);
        settings_ = settings;
    }

    TradeSettings GetSettings() const {
        std::lock_guard<std::mutex> lock(trade_mutex_);
        return settings_;
    }

    // Thread-safe Outgoing Command Queue
    void QueueCommand(const std::string& cmd_json) {
        std::lock_guard<std::mutex> lock(cmd_mutex_);
        command_queue_.push_back(cmd_json);
    }

    std::vector<std::string> PopCommands() {
        std::lock_guard<std::mutex> lock(cmd_mutex_);
        std::vector<std::string> cmds = std::move(command_queue_);
        command_queue_.clear();
        return cmds;
    }

private:
    std::atomic<EngineState> engine_state_{EngineState::ARMED};
    std::atomic<bool> zmq_connected_{false};
    std::atomic<uint64_t> received_frames_count_{0};

    mutable std::mutex frame_mutex_;
    std::map<std::string, MarketFrame> frames_;
    std::string active_symbol_{"XAUUSD"};

    mutable std::mutex log_mutex_;
    std::deque<ConsoleLogEntry> logs_;

    mutable std::mutex trade_mutex_;
    std::vector<TradePosition> positions_;
    std::vector<TradeHistoryItem> history_;
    TradeSettings settings_;

    mutable std::mutex cmd_mutex_;
    std::vector<std::string> command_queue_;
};
