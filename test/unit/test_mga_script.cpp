// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "mga/mga_script.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

using y8960upd::mga::MgaScript;

namespace {

std::vector<std::uint8_t> bytes_of(const MgaScript& s) { return {s.bytes().begin(), s.bytes().end()}; }

}  // namespace

TEST_CASE("each instruction is 4 bytes: op, address high, address low, data")
{
    MgaScript s;
    s.read_io(0x40);
    s.write_io(0x46, 0x01);
    s.compare(0x7F);
    s.and_(0x01);
    s.jump_back(2);
    s.jump_back(4);
    s.wait_us(20);
    s.push(0xC001);
    s.abort();
    s.end();
    const std::vector<std::uint8_t> want = {
        0x03, 0x00, 0x40, 0x00,
        0x04, 0x00, 0x46, 0x01,
        0x06, 0x00, 0x00, 0x7F,
        0x07, 0x00, 0x00, 0x01,
        0x0A, 0x00, 0x00, 0xFE,
        0x0A, 0x00, 0x00, 0xFC,
        0x05, 0x00, 0x14, 0x00,
        0x0B, 0xC0, 0x01, 0x00,
        0xFE, 0x00, 0x00, 0x00,
        0xFF, 0x00, 0x00, 0x00,
    };
    CHECK(bytes_of(s) == want);
    CHECK(s.instruction_count() == 10);
}

TEST_CASE("jump_back accepts 1 to 128 instructions")
{
    MgaScript s;
    s.jump_back(1);
    s.jump_back(128);
    CHECK(bytes_of(s) == std::vector<std::uint8_t>{0x0A, 0x00, 0x00, 0xFF, 0x0A, 0x00, 0x00, 0x80});
    CHECK_THROWS_AS(s.jump_back(0), std::invalid_argument);
    CHECK_THROWS_AS(s.jump_back(129), std::invalid_argument);
}
