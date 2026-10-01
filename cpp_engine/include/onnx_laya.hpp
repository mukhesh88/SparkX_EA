#pragma once

#include "app_state.hpp"
#include "modernbert_tokenizer.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

class LayaONNXEngine {
public:
    explicit LayaONNXEngine(
        const std::string& model_path = "models/laya.onnx",
        int gpu_device_id = 0
    );
    ~LayaONNXEngine();

    // Initializes Ort::Env, Ort::SessionOptions, CUDAExecutionProvider, and loads model weights
    bool Initialize();

    bool IsGPUAccelerated() const { return gpu_accelerated_; }
    bool IsModelLoaded() const { return is_model_loaded_; }
    std::string GetProviderName() const { return provider_name_; }
    const ModernBERTTokenizer& GetTokenizer() const { return tokenizer_; }

    // Tokenizes SMC context string (<512 tokens) and runs live inference through Ort::Session
    LayaOutput InferPrimitives(const std::string& compressed_market_state);

private:
    std::string model_path_;
    int gpu_device_id_;
    bool is_initialized_ = false;
    bool is_model_loaded_ = false;
    bool gpu_accelerated_ = false;
    std::string provider_name_ = "Uninitialized";

    ModernBERTTokenizer tokenizer_;

    // PIMPL idiom to encapsulate ONNX Runtime types and memory management
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};
