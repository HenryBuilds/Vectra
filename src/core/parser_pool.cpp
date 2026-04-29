// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "parser_pool.hpp"

#include <stdexcept>
#include <utility>

#include <fmt/format.h>
#include <tree_sitter/api.h>

#include "vectra/core/language.hpp"

namespace vectra::core {

// ---------------------------------------------------------------------------
// ParserLease
// ---------------------------------------------------------------------------

ParserLease::ParserLease(ParserPool* owner, std::string language, TSParser* parser)
    : owner_(owner), language_(std::move(language)), parser_(parser) {}

ParserLease::ParserLease(ParserLease&& other) noexcept
    : owner_(other.owner_),
      language_(std::move(other.language_)),
      parser_(other.parser_) {
    other.owner_  = nullptr;
    other.parser_ = nullptr;
}

ParserLease& ParserLease::operator=(ParserLease&& other) noexcept {
    if (this != &other) {
        if (parser_ != nullptr && owner_ != nullptr) {
            owner_->release(language_, parser_);
        }
        owner_    = other.owner_;
        language_ = std::move(other.language_);
        parser_   = other.parser_;
        other.owner_  = nullptr;
        other.parser_ = nullptr;
    }
    return *this;
}

ParserLease::~ParserLease() {
    if (parser_ != nullptr && owner_ != nullptr) {
        owner_->release(language_, parser_);
    }
}

// ---------------------------------------------------------------------------
// ParserPool
// ---------------------------------------------------------------------------

ParserPool::ParserPool(const LanguageRegistry& registry) : registry_(registry) {}

ParserPool::~ParserPool() {
    // The pool owns every parser that was returned to it. Anything
    // currently leased out will be deleted by the lease's destructor;
    // we only clean up the idle reserve here.
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto& [_, parsers] : available_) {
        for (TSParser* p : parsers) {
            ts_parser_delete(p);
        }
    }
    available_.clear();
}

ParserLease ParserPool::acquire(std::string_view language) {
    const Language* lang = registry_.by_name(language);
    if (lang == nullptr) {
        throw std::runtime_error(fmt::format(
            "ParserPool::acquire: unknown language '{}'", language));
    }

    TSParser* parser = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto& reserve = available_[lang->name];
        if (!reserve.empty()) {
            parser = reserve.back();
            reserve.pop_back();
        }
    }

    if (parser == nullptr) {
        parser = ts_parser_new();
        if (parser == nullptr) {
            throw std::runtime_error("ts_parser_new returned null");
        }
    }

    if (!ts_parser_set_language(parser, lang->ts_language)) {
        ts_parser_delete(parser);
        throw std::runtime_error(fmt::format(
            "ts_parser_set_language failed for '{}'. This usually means the "
            "grammar's ABI version does not match the linked tree-sitter runtime.",
            lang->name));
    }

    return ParserLease(this, lang->name, parser);
}

void ParserPool::release(const std::string& language, TSParser* parser) noexcept {
    if (parser == nullptr) return;

    // Reset parser state — the next user gets a clean slate. Resetting
    // the language is a no-op since acquire() always re-sets it.
    ts_parser_reset(parser);

    std::lock_guard<std::mutex> guard(mutex_);
    available_[language].push_back(parser);
}

}  // namespace vectra::core
