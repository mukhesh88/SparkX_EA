#pragma once

#include "app_state.hpp"
#include "onnx_laya.hpp"
#include <string>
#include <thread>
#include <atomic>
#include <memory>

class ZMQListenerWorker {
public:
    ZMQListenerWorker(
        ThreadSafeAppState& app_state,
        LayaONNXEngine& onnx_engine,
        const std::string& connect_endpoint = "tcp://127.0.0.1:5555"
    );
    ~ZMQListenerWorker();

    void Start();
    void Stop();
    bool IsRunning() const { return is_running_.load(); }

private:
    void RunLoop();
    void ProcessMessagePayload(const std::string& json_payload, int sock = -1);

    ThreadSafeAppState& state_;
    LayaONNXEngine& onnx_;
    std::string endpoint_;

    std::atomic<bool> is_running_{false};
    std::atomic<bool> stop_signal_{false};
    std::thread worker_thread_;
};
