#include "zmq_listener.hpp"
#include <iostream>
#include <chrono>
#include <sstream>
#include <cmath>
#include <algorithm>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/tcp.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define closesocket close
#endif

#if defined(HAS_ZMQ)
#include <zmq.hpp>
#endif

ZMQListenerWorker::ZMQListenerWorker(
    ThreadSafeAppState& app_state,
    LayaONNXEngine& onnx_engine,
    const std::string& connect_endpoint
) : state_(app_state), onnx_(onnx_engine), endpoint_(connect_endpoint) {
}

ZMQListenerWorker::~ZMQListenerWorker() {
    Stop();
}

void ZMQListenerWorker::Start() {
    if (is_running_.load()) return;
    stop_signal_.store(false);
    is_running_.store(true);
    worker_thread_ = std::thread(&ZMQListenerWorker::RunLoop, this);
    state_.AddLog("INFO", "Market data listener thread launched for " + endpoint_);
}

void ZMQListenerWorker::Stop() {
    if (!is_running_.load()) return;
    stop_signal_.store(true);
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    is_running_.store(false);
    state_.SetZmqConnected(false);
    state_.AddLog("INFO", "Market data listener thread stopped.");
}

static std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) pos++;
    if (pos >= json.size() || json[pos] != '\"') return "";
    
    std::string result;
    size_t cur = pos + 1;
    while (cur < json.size()) {
        if (json[cur] == '\\') {
            if (cur + 1 < json.size()) {
                char next = json[cur + 1];
                if (next == 'n') result += '\n';
                else if (next == 't') result += '\t';
                else if (next == 'r') result += '\r';
                else if (next == '\"') result += '\"';
                else if (next == '\\') result += '\\';
                else result += next;
                cur += 2;
                continue;
            }
        } else if (json[cur] == '\"') {
            break;
        } else {
            result += json[cur];
        }
        cur++;
    }
    return result;
}

static double ExtractJsonNumber(const std::string& json, const std::string& key, double default_val = 0.0) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return default_val;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return default_val;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) pos++;
    if (pos >= json.size()) return default_val;
    
    size_t end_pos = json.find_first_of(",}\n\r", pos);
    if (end_pos == std::string::npos) end_pos = json.size();
    std::string num_str = json.substr(pos, end_pos - pos);
    try {
        return std::stod(num_str);
    } catch (...) {
        return default_val;
    }
}

static bool ExtractJsonBool(const std::string& json, const std::string& key, bool default_val = false) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return default_val;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return default_val;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) pos++;
    if (pos >= json.size()) return default_val;
    if (json.compare(pos, 4, "true") == 0) return true;
    if (json.compare(pos, 5, "false") == 0) return false;
    return default_val;
}

static std::vector<std::string> ExtractJsonArray(const std::string& json, const std::string& key) {
    std::vector<std::string> items;
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return items;
    pos = json.find('[', pos + pattern.size());
    if (pos == std::string::npos) return items;
    pos++;
    
    int depth = 0;
    size_t obj_start = std::string::npos;
    for (size_t i = pos; i < json.size(); ++i) {
        char c = json[i];
        if (c == '{') {
            if (depth == 0) obj_start = i;
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0 && obj_start != std::string::npos) {
                items.push_back(json.substr(obj_start, i - obj_start + 1));
                obj_start = std::string::npos;
            }
        } else if (c == ']' && depth == 0) {
            break;
        }
    }
    return items;
}

void ZMQListenerWorker::ProcessMessagePayload(const std::string& json_payload, int sock) {
    // Check if this is an event broadcast (e.g., SL Retrain complete or settings update)
    std::string evt = ExtractJsonString(json_payload, "event");
    if (evt == "SL_RETRAIN_COMPLETE") {
        std::string rc = ExtractJsonString(json_payload, "root_cause");
        std::string rect = ExtractJsonString(json_payload, "rectification");
        double loss = ExtractJsonNumber(json_payload, "loss", 0.0);
        int samples = (int)ExtractJsonNumber(json_payload, "samples_trained", 0);
        double ticket = ExtractJsonNumber(json_payload, "ticket", 0);

        std::stringstream ss;
        ss << "[SL RETRAIN COMPLETED] Ticket #" << (uint64_t)ticket << " | Failure: " << rc 
           << " | Rectification: " << rect << " | Loss: " << loss << " (" << samples << " samples)";
        state_.AddLog("ALERT", ss.str());

        // Hot-reload the newly exported weights into ONNX Runtime session
        bool reloaded = onnx_.ReloadModel();
        if (reloaded) {
            state_.AddLog("EXEC", "[Laya Neural Engine] Retrained ONNX weights successfully hot-reloaded into live inference pipeline.");
        }
        return;
    }

    MarketFrame frame;

    // Flush any pending commands queued from C++ UI
    if (sock >= 0) {
        auto outgoing = state_.PopCommands();
        for (const auto& cmd : outgoing) {
            std::string msg = cmd + "\n";
            send(sock, msg.c_str(), (int)msg.size(), 0);
        }
    }

    // Parse active open positions if present
    auto pos_objs = ExtractJsonArray(json_payload, "positions");
    if (!pos_objs.empty() || json_payload.find("\"positions\": []") != std::string::npos || json_payload.find("\"positions\":[]") != std::string::npos) {
        std::vector<TradePosition> positions;
        for (const auto& obj : pos_objs) {
            TradePosition p;
            p.ticket = (uint64_t)ExtractJsonNumber(obj, "ticket", 0);
            p.time = (uint64_t)ExtractJsonNumber(obj, "time", 0);
            p.time_str = ExtractJsonString(obj, "time_str");
            p.symbol = ExtractJsonString(obj, "symbol");
            if (p.symbol.empty()) p.symbol = "XAUUSD";
            p.type = ExtractJsonString(obj, "type");
            p.volume = ExtractJsonNumber(obj, "volume", 0.1);
            p.price_open = ExtractJsonNumber(obj, "price_open", 0.0);
            p.price_current = ExtractJsonNumber(obj, "price_current", 0.0);
            p.sl = ExtractJsonNumber(obj, "sl", 0.0);
            p.tp = ExtractJsonNumber(obj, "tp", 0.0);
            p.profit = ExtractJsonNumber(obj, "profit", 0.0);
            p.comment = ExtractJsonString(obj, "comment");
            positions.push_back(p);
        }
        state_.UpdatePositions(positions);
    }

    // Parse completed trade history if present
    auto hist_objs = ExtractJsonArray(json_payload, "history");
    if (!hist_objs.empty()) {
        std::vector<TradeHistoryItem> history;
        for (const auto& obj : hist_objs) {
            TradeHistoryItem h;
            h.ticket = (uint64_t)ExtractJsonNumber(obj, "ticket", 0);
            h.time = (uint64_t)ExtractJsonNumber(obj, "time", 0);
            h.time_str = ExtractJsonString(obj, "time_str");
            h.symbol = ExtractJsonString(obj, "symbol");
            if (h.symbol.empty()) h.symbol = "XAUUSD";
            h.type = ExtractJsonString(obj, "type");
            h.volume = ExtractJsonNumber(obj, "volume", 0.1);
            h.price_open = ExtractJsonNumber(obj, "price_open", 0.0);
            h.price_close = ExtractJsonNumber(obj, "price_close", 0.0);
            h.profit = ExtractJsonNumber(obj, "profit", 0.0);
            h.outcome = ExtractJsonString(obj, "outcome");
            if (h.outcome.empty()) h.outcome = (h.profit >= 0.0) ? "WIN" : "LOSS";
            h.comment = ExtractJsonString(obj, "comment");
            history.push_back(h);
        }
        state_.UpdateHistory(history);
    }

    // Check for incoming execution notifications from Python MT5 Gateway
    bool exec_done = ExtractJsonBool(json_payload, "executed", false);
    if (exec_done) {
        double ticket = ExtractJsonNumber(json_payload, "order_id", 0.0);
        std::string action = ExtractJsonString(json_payload, "action");
        double px = ExtractJsonNumber(json_payload, "price", 0.0);
        double vol = ExtractJsonNumber(json_payload, "volume", 0.0);
        double sl = ExtractJsonNumber(json_payload, "sl", 0.0);
        double tp = ExtractJsonNumber(json_payload, "tp", 0.0);
        static double s_last_logged_ticket = 0;
        if (ticket > 0 && ticket != s_last_logged_ticket) {
            s_last_logged_ticket = ticket;
            std::stringstream ss;
            ss << "[LIVE MT5 FILLED] Ticket #" << (uint64_t)ticket << " " << action 
               << " " << vol << " lots @ $" << px << " | SL: $" << sl << " | TP: $" << tp;
            state_.AddLog("EXEC", ss.str());
        }
    }

    // Symbol & Pricing
    std::string sym = ExtractJsonString(json_payload, "symbol");
    frame.symbol = sym.empty() ? "XAUUSD" : sym;

    frame.price = ExtractJsonNumber(json_payload, "price", 0.0);
    frame.bid = ExtractJsonNumber(json_payload, "bid", 0.0);
    frame.ask = ExtractJsonNumber(json_payload, "ask", 0.0);
    frame.spread = static_cast<float>(ExtractJsonNumber(json_payload, "spread", 1.2));
    frame.session = ExtractJsonString(json_payload, "session");
    if (frame.session.empty()) frame.session = "NY_OVERLAP";

    frame.token_count = static_cast<int>(ExtractJsonNumber(json_payload, "token_count", 120));
    frame.compressed_state = ExtractJsonString(json_payload, "compressed_state");
    frame.ingest_latency_ms = static_cast<float>(ExtractJsonNumber(json_payload, "latency_ms", 0.5));

    frame.is_live_feed = ExtractJsonBool(json_payload, "is_live", true);
    std::string ds = ExtractJsonString(json_payload, "data_source");
    frame.feed_source = ExtractJsonString(json_payload, "broker");
    if (!ds.empty()) {
        if (ds == "TRADINGVIEW_LIVE") {
            frame.feed_source = "TRADINGVIEW REAL SPOT";
        } else if (ds == "MT5_LIVE") {
            frame.feed_source = "MT5 LIVE BROKER";
        }
    }
    if (frame.feed_source.empty()) {
        frame.feed_source = frame.is_live_feed ? "MT5 LIVE BROKER" : "TRADINGVIEW REAL SPOT";
    }

    // Bias
    std::string h1_str = ExtractJsonString(json_payload, "h1_trend");
    frame.bias.h1_trend = h1_str.empty() ? "BULLISH" : h1_str;

    std::string m15_str = ExtractJsonString(json_payload, "m15_struct");
    frame.bias.m15_struct = m15_str.empty() ? "BOS_BULLISH" : m15_str;

    std::string m5_str = ExtractJsonString(json_payload, "m5_struct");
    frame.bias.m5_struct = m5_str.empty() ? "MSS_BULLISH" : m5_str;

    std::string zone_str = ExtractJsonString(json_payload, "zone");
    frame.bias.zone = zone_str.empty() ? "DISCOUNT" : zone_str;

    frame.bias.fib_pct = static_cast<float>(ExtractJsonNumber(json_payload, "fib_pct", 50.0));

    // Liquidity
    frame.liquidity.asia_high = ExtractJsonNumber(json_payload, "asia_high", 0.0);
    frame.liquidity.asia_low = ExtractJsonNumber(json_payload, "asia_low", 0.0);
    frame.liquidity.bsl_swept = ExtractJsonBool(json_payload, "bsl_swept", false);
    frame.liquidity.ssl_swept = ExtractJsonBool(json_payload, "ssl_swept", false);
    frame.liquidity.bsl_target = ExtractJsonNumber(json_payload, "bsl_target", 0.0);
    frame.liquidity.ssl_target = ExtractJsonNumber(json_payload, "ssl_target", 0.0);

    // Arrays
    frame.arrays.fvg_active = ExtractJsonBool(json_payload, "fvg_active", false);
    frame.arrays.fvg_direction = ExtractJsonString(json_payload, "fvg_direction");
    frame.arrays.ob_active = ExtractJsonBool(json_payload, "ob_active", false);
    frame.arrays.ob_direction = ExtractJsonString(json_payload, "ob_direction");

    // Adaptive SL Failure Attribution & Rectification
    frame.adaptive_rectification = ExtractJsonString(json_payload, "adaptive_rectification");
    if (frame.adaptive_rectification.empty()) frame.adaptive_rectification = "None";
    frame.adaptive_root_cause = ExtractJsonString(json_payload, "adaptive_root_cause");
    if (frame.adaptive_root_cause.empty()) frame.adaptive_root_cause = "NONE";

    // Evaluate real Laya ONNX inference on the received state
    if (state_.GetEngineState() != EngineState::EMERGENCY_KILL) {
        frame.laya_decision = onnx_.InferPrimitives(frame.compressed_state);

        // Check if trade action triggered
        if (frame.laya_decision.choice_action != "HOLD" &&
            frame.laya_decision.choice_confidence >= 0.85f &&
            frame.laya_decision.score_grade >= 7.5f) {

            // Check if position already exists for this asset (strictly prevent duplicate stacking)
            bool already_open = false;
            auto is_same_asset = [](const std::string& a, const std::string& b) {
                if (a == b) return true;
                bool a_gold = a.find("XAU") != std::string::npos || a.find("GOLD") != std::string::npos;
                bool b_gold = b.find("XAU") != std::string::npos || b.find("GOLD") != std::string::npos;
                if (a_gold && b_gold) return true;
                bool a_btc = a.find("BTC") != std::string::npos;
                bool b_btc = b.find("BTC") != std::string::npos;
                if (a_btc && b_btc) return true;
                return false;
            };
            for (const auto& pos : state_.GetPositions()) {
                if (is_same_asset(pos.symbol, frame.symbol)) {
                    already_open = true;
                    break;
                }
            }

            // Institutional Spacing Latch: minimum 300 seconds (5 minutes) between automated dispatches
            static std::chrono::steady_clock::time_point last_dispatch_time =
                std::chrono::steady_clock::now() - std::chrono::seconds(600);
            auto now_time = std::chrono::steady_clock::now();
            auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(now_time - last_dispatch_time).count();

            if (!already_open && elapsed_sec >= 300) {
                last_dispatch_time = now_time;
                std::stringstream ss;
                ss << "[EXECUTION GATE PASSED] " << frame.symbol << " " << frame.laya_decision.choice_action
                   << " (" << (int)(frame.laya_decision.choice_confidence * 100) << "%) | Setup Grade: "
                   << frame.laya_decision.score_grade << "/10.0 | Latency: "
                   << frame.laya_decision.inference_latency_ms << "ms";
                state_.AddLog("EXEC", ss.str());

                // If terminal is ARMED, dispatch execution command to Python MT5 Gateway
                if (state_.GetEngineState() == EngineState::ARMED && sock >= 0) {
                    std::string cmd = "{\"command\":\"EXECUTE\",\"symbol\":\"" + frame.symbol + "\",\"action\":\"" + frame.laya_decision.choice_action + "\"}\n";
                    send(sock, cmd.c_str(), (int)cmd.size(), 0);
                }
            }
        }
    } else {
        frame.laya_decision.choice_action = "HOLD (EMERGENCY_HALT)";
        frame.laya_decision.choice_confidence = 1.0f;
    }

    state_.UpdateMarketFrame(frame);
}

void ZMQListenerWorker::RunLoop() {
#if defined(_WIN32)
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        state_.AddLog("WARN", "WSAStartup failed on Windows platform.");
        return;
    }
#endif

    // Parse host and port from endpoint_ (e.g., "tcp://127.0.0.1:5556" or "127.0.0.1:5556")
    std::string host = "127.0.0.1";
    int port = 5556;
    std::string ep = endpoint_;
    size_t proto_pos = ep.find("://");
    if (proto_pos != std::string::npos) {
        ep = ep.substr(proto_pos + 3);
    }
    size_t colon_pos = ep.find(':');
    if (colon_pos != std::string::npos) {
        host = ep.substr(0, colon_pos);
        try {
            port = std::stoi(ep.substr(colon_pos + 1));
        } catch (...) {
            port = 5556;
        }
    }

    int retry_count = 0;
    while (!stop_signal_.load()) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        int flag = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));

#if defined(_WIN32)
        DWORD rcv_timeout = 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&rcv_timeout, sizeof(rcv_timeout));
#else
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
#endif

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);

        if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
            closesocket(sock);
            state_.SetZmqConnected(false);
            if (retry_count % 4 == 0) {
                state_.AddLog("INFO", "Awaiting live MT5 stream on " + host + ":" + std::to_string(port) + " (connecting...)");
            }
            retry_count++;
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }

        // Successfully connected to live publisher!
        retry_count = 0;
        state_.SetZmqConnected(true);
        state_.AddLog("INFO", "Connected to Live MT5 Stream on " + host + ":" + std::to_string(port));

        char buf[8192];
        std::string stream_buffer;

        while (!stop_signal_.load()) {
            int bytes_read = recv(sock, buf, sizeof(buf) - 1, 0);
            if (bytes_read > 0) {
                buf[bytes_read] = '\0';
                stream_buffer.append(buf, bytes_read);

                size_t nl;
                while ((nl = stream_buffer.find('\n')) != std::string::npos) {
                    std::string line = stream_buffer.substr(0, nl);
                    stream_buffer.erase(0, nl + 1);
                    if (!line.empty() && line.front() == '{') {
                        ProcessMessagePayload(line, sock);
                    }
                }
            } else if (bytes_read == 0) {
                state_.AddLog("WARN", "Market stream disconnected by publisher. Reconnecting...");
                break;
            } else {
#if defined(_WIN32)
                int err = WSAGetLastError();
                if (err != WSAETIMEDOUT) {
                    state_.AddLog("WARN", "Market stream socket error (" + std::to_string(err) + "). Reconnecting...");
                    break;
                } else {
                    auto outgoing = state_.PopCommands();
                    for (const auto& cmd : outgoing) {
                        std::string msg = cmd + "\n";
                        send(sock, msg.c_str(), (int)msg.size(), 0);
                    }
                }
#else
                auto outgoing = state_.PopCommands();
                for (const auto& cmd : outgoing) {
                    std::string msg = cmd + "\n";
                    send(sock, msg.c_str(), (int)msg.size(), 0);
                }
                break;
#endif
            }
        }

        closesocket(sock);
        state_.SetZmqConnected(false);
        if (!stop_signal_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

#if defined(_WIN32)
    WSACleanup();
#endif
}
