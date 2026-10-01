#include "modernbert_tokenizer.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

ModernBERTTokenizer::ModernBERTTokenizer() {
    InitializeDefaultVocab();
}

ModernBERTTokenizer::ModernBERTTokenizer(const std::string& vocab_or_json_path) {
    InitializeDefaultVocab();
    if (!vocab_or_json_path.empty()) {
        LoadVocabulary(vocab_or_json_path);
    }
}

ModernBERTTokenizer::~ModernBERTTokenizer() = default;

void ModernBERTTokenizer::InitializeDefaultVocab() {
    vocab_to_id_.clear();
    id_to_vocab_.clear();

    // Special Tokens
    vocab_to_id_["[PAD]"] = PAD_TOKEN_ID;
    vocab_to_id_["[CLS]"] = CLS_TOKEN_ID;
    vocab_to_id_["[SEP]"] = SEP_TOKEN_ID;
    vocab_to_id_["[UNK]"] = UNK_TOKEN_ID;

    // High-frequency financial & SMC tokens
    const std::vector<std::string> domain_tokens = {
        "<", ">", "/", "|", ":", "[", "]", "(", ")", "=", ",", ".", "-", "+", "%",
        "MKT_STATE", "sym", "time", "px", "spd", "sess", "pts", "Z",
        "BIAS", "H1", "M15", "M5", "BULLISH", "BEARISH", "RANGE", "NEUTRAL",
        "BOS_BULLISH", "BOS_BEARISH", "MSS_BULLISH", "MSS_BEARISH",
        "DEALING_RANGE", "LO", "HI", "EQ", "ZONE", "DISCOUNT", "PREMIUM", "EQUILIBRIUM",
        "LIQUIDITY", "ASIA_H", "ASIA_L", "SWEEP", "SSL_SWEPT", "BSL_SWEPT", "BOTH",
        "BSL_TGT", "SSL_TGT", "NONE",
        "SMC_ARRAYS", "FVG", "OB", "CE", "ACTIVE", "UNTESTED", "MITIGATED",
        "MOMENTUM", "ATR", "DISP", "VOL_EXP", "T", "F",
        "TASK", "CHOICE_SELECTION", "SETUP_QUALITY_SCORE", "NOUL_HYPOTHESIS_CHECK",
        "MARKET_BUY", "MARKET_SELL", "LIMIT_BUY_ORDER_BLOCK", "LIMIT_SELL_ORDER_BLOCK", "HOLD",
        "XAUUSD", "BTCUSD", "NY_OVERLAP", "LONDON", "ASIAN"
    };

    int64_t id_counter = 100;
    for (const auto& tok : domain_tokens) {
        vocab_to_id_[tok] = id_counter;
        id_to_vocab_[id_counter] = tok;
        id_counter++;
    }

    // Byte fallback tokens (0..255)
    for (int i = 0; i < 256; ++i) {
        std::string byte_tok = "<0x" + std::to_string(i) + ">";
        int64_t byte_id = 1000 + i;
        vocab_to_id_[byte_tok] = byte_id;
        id_to_vocab_[byte_id] = byte_tok;
    }
}

bool ModernBERTTokenizer::LoadVocabulary(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    int64_t id = 0;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        vocab_to_id_[line] = id;
        id_to_vocab_[id] = line;
        id++;
    }
    is_custom_vocab_loaded_ = true;
    return true;
}

std::vector<int64_t> ModernBERTTokenizer::SubwordTokenize(const std::string& word) const {
    std::vector<int64_t> tokens;
    if (word.empty()) return tokens;

    auto it = vocab_to_id_.find(word);
    if (it != vocab_to_id_.end()) {
        tokens.push_back(it->second);
        return tokens;
    }

    // Byte fallback decomposition
    for (unsigned char c : word) {
        std::string byte_str = "<0x" + std::to_string((int)c) + ">";
        auto byte_it = vocab_to_id_.find(byte_str);
        if (byte_it != vocab_to_id_.end()) {
            tokens.push_back(byte_it->second);
        } else {
            tokens.push_back(UNK_TOKEN_ID);
        }
    }
    return tokens;
}

TokenizedSequence ModernBERTTokenizer::Encode(
    const std::string& text,
    size_t max_length,
    bool pad_to_max
) const {
    TokenizedSequence seq;
    seq.input_ids.reserve(max_length);
    seq.attention_mask.reserve(max_length);

    // 1. Prepend [CLS] special token
    seq.input_ids.push_back(CLS_TOKEN_ID);
    seq.attention_mask.push_back(1);

    // 2. Pre-tokenize text by delimiters and whitespace
    std::string current_token;
    auto flush_token = [&](std::string& tok) {
        if (tok.empty()) return;
        auto sub_ids = SubwordTokenize(tok);
        for (int64_t sub_id : sub_ids) {
            // Reserve space for final [SEP]
            if (seq.input_ids.size() < max_length - 1) {
                seq.input_ids.push_back(sub_id);
                seq.attention_mask.push_back(1);
            }
        }
        tok.clear();
    };

    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            flush_token(current_token);
        } else if (c == '|' || c == ':' || c == '[' || c == ']' || c == '(' || c == ')' || 
                   c == '<' || c == '>' || c == '=' || c == ',' || c == '\'' || c == '\"') {
            flush_token(current_token);
            std::string delim(1, c);
            flush_token(delim);
        } else {
            current_token += c;
        }

        // Hard truncation check
        if (seq.input_ids.size() >= max_length - 1) {
            break;
        }
    }
    flush_token(current_token);

    // 3. Append [SEP] special token
    if (seq.input_ids.size() < max_length) {
        seq.input_ids.push_back(SEP_TOKEN_ID);
        seq.attention_mask.push_back(1);
    } else {
        seq.input_ids.back() = SEP_TOKEN_ID;
    }

    seq.actual_length = seq.input_ids.size();

    // 4. Pad remaining sequence to max_length (512 tokens)
    if (pad_to_max) {
        while (seq.input_ids.size() < max_length) {
            seq.input_ids.push_back(PAD_TOKEN_ID);
            seq.attention_mask.push_back(0); // Mask out padding
        }
    }

    return seq;
}
