#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <memory>

struct TokenizedSequence {
    std::vector<int64_t> input_ids;
    std::vector<int64_t> attention_mask;
    size_t actual_length = 0;
};

class ModernBERTTokenizer {
public:
    static constexpr size_t MAX_CONTEXT_TOKENS = 512;
    static constexpr int64_t CLS_TOKEN_ID = 50281;
    static constexpr int64_t SEP_TOKEN_ID = 50282;
    static constexpr int64_t PAD_TOKEN_ID = 50283;
    static constexpr int64_t UNK_TOKEN_ID = 50284;

    ModernBERTTokenizer();
    explicit ModernBERTTokenizer(const std::string& vocab_or_json_path);
    ~ModernBERTTokenizer();

    bool LoadVocabulary(const std::string& path);

    // Tokenizes SMC context string, strictly enforcing the 512-token limit
    TokenizedSequence Encode(
        const std::string& text,
        size_t max_length = MAX_CONTEXT_TOKENS,
        bool pad_to_max = true
    ) const;

    size_t GetVocabSize() const { return vocab_to_id_.size(); }

private:
    void InitializeDefaultVocab();
    std::vector<int64_t> SubwordTokenize(const std::string& word) const;

    std::unordered_map<std::string, int64_t> vocab_to_id_;
    std::unordered_map<int64_t, std::string> id_to_vocab_;
    bool is_custom_vocab_loaded_ = false;
};
