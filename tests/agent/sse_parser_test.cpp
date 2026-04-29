// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "sse_parser.hpp"  // resolved via the test target's include path

#include <catch2/catch_test_macros.hpp>

using vectra::agent::detail::SseEvent;
using vectra::agent::detail::SseParser;

TEST_CASE("SseParser extracts a complete event delivered in one feed", "[sse]") {
    SseParser parser;
    const auto events = parser.feed("event: ping\ndata: {\"a\":1}\n\n");

    REQUIRE(events.size() == 1);
    REQUIRE(events[0].type == "ping");
    REQUIRE(events[0].data == "{\"a\":1}");
}

TEST_CASE("SseParser handles events split across feeds", "[sse]") {
    SseParser parser;

    const auto a = parser.feed("event: pi");
    REQUIRE(a.empty());

    const auto b = parser.feed("ng\ndata: {\"a\":");
    REQUIRE(b.empty());

    const auto c = parser.feed("1}\n\n");
    REQUIRE(c.size() == 1);
    REQUIRE(c[0].type == "ping");
    REQUIRE(c[0].data == "{\"a\":1}");
}

TEST_CASE("SseParser concatenates multiple data: lines with newlines", "[sse]") {
    SseParser parser;
    const auto events = parser.feed(
        "event: multi\n"
        "data: first\n"
        "data: second\n"
        "data: third\n"
        "\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].type == "multi");
    REQUIRE(events[0].data == "first\nsecond\nthird");
}

TEST_CASE("SseParser yields default-typed event when 'event:' is omitted", "[sse]") {
    SseParser parser;
    const auto events = parser.feed("data: hello\n\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].type.empty());
    REQUIRE(events[0].data == "hello");
}

TEST_CASE("SseParser strips at most one leading space after the colon", "[sse]") {
    SseParser parser;
    const auto events = parser.feed(
        "data: one space\n"
        "\n"
        "data:  two spaces — keep one\n"
        "\n");
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].data == "one space");
    REQUIRE(events[1].data == " two spaces — keep one");
}

TEST_CASE("SseParser ignores comment lines", "[sse]") {
    SseParser parser;
    const auto events = parser.feed(
        ": this is a keep-alive\n"
        "event: real\n"
        "data: payload\n"
        "\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].type == "real");
    REQUIRE(events[0].data == "payload");
}

TEST_CASE("SseParser tolerates CRLF line endings", "[sse]") {
    SseParser parser;
    const auto events = parser.feed(
        "event: crlf\r\n"
        "data: payload\r\n"
        "\r\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].type == "crlf");
    REQUIRE(events[0].data == "payload");
}

TEST_CASE("SseParser emits multiple events from one feed in order", "[sse]") {
    SseParser parser;
    const auto events = parser.feed(
        "event: a\n"
        "data: 1\n"
        "\n"
        "event: b\n"
        "data: 2\n"
        "\n"
        "event: c\n"
        "data: 3\n"
        "\n");
    REQUIRE(events.size() == 3);
    REQUIRE(events[0].type == "a");
    REQUIRE(events[0].data == "1");
    REQUIRE(events[1].type == "b");
    REQUIRE(events[1].data == "2");
    REQUIRE(events[2].type == "c");
    REQUIRE(events[2].data == "3");
}

TEST_CASE("SseParser holds onto an unterminated trailing event", "[sse]") {
    SseParser parser;

    // Last event has no blank-line terminator yet; should not emit.
    const auto first = parser.feed(
        "event: done\n"
        "data: x\n"
        "\n"
        "event: pending\n"
        "data: y\n");
    REQUIRE(first.size() == 1);
    REQUIRE(first[0].type == "done");

    // Next feed delivers the missing blank line.
    const auto second = parser.feed("\n");
    REQUIRE(second.size() == 1);
    REQUIRE(second[0].type == "pending");
    REQUIRE(second[0].data == "y");
}

TEST_CASE("SseParser drops a blank-line at the start without emitting an event", "[sse]") {
    SseParser parser;
    const auto events = parser.feed("\n\nevent: real\ndata: x\n\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].type == "real");
}
