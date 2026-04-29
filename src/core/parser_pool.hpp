// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Thread-safe pool of TSParser instances, keyed by language. A
// TSParser holds parser state (cache, allocator) and is not safe to
// share across threads in flight, but creating one per parse is
// expensive — so we recycle.
//
// Usage:
//   auto lease = pool.acquire("python");
//   ts_parser_set_language(lease.get(), python_lang);
//   TSTree* tree = ts_parser_parse_string(lease.get(), nullptr, src, len);
//   // ... lease destructor returns parser to pool
//
// Internal to vectra-core; not exposed in include/vectra/core/.

#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

extern "C" {
typedef struct TSParser TSParser;
}

namespace vectra::core {

class LanguageRegistry;
class ParserPool;

// RAII lease on a parser. Destructor returns the parser to the pool.
class ParserLease {
public:
    ParserLease(const ParserLease&) = delete;
    ParserLease& operator=(const ParserLease&) = delete;
    ParserLease(ParserLease&& other) noexcept;
    ParserLease& operator=(ParserLease&& other) noexcept;
    ~ParserLease();

    [[nodiscard]] TSParser* get() const noexcept { return parser_; }
    [[nodiscard]] TSParser* operator->() const noexcept { return parser_; }

private:
    friend class ParserPool;
    ParserLease(ParserPool* owner, std::string language, TSParser* parser);

    ParserPool* owner_ = nullptr;
    std::string language_;
    TSParser* parser_ = nullptr;
};

class ParserPool {
public:
    explicit ParserPool(const LanguageRegistry& registry);
    ~ParserPool();

    ParserPool(const ParserPool&) = delete;
    ParserPool& operator=(const ParserPool&) = delete;

    // Acquire a parser for `language`. The parser already has its
    // ts_parser_set_language() set to the matching grammar. If the
    // pool has a free parser, returns it; otherwise creates a new one.
    [[nodiscard]] ParserLease acquire(std::string_view language);

private:
    friend class ParserLease;
    void release(const std::string& language, TSParser* parser) noexcept;

    const LanguageRegistry& registry_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::vector<TSParser*>> available_;
};

}  // namespace vectra::core
