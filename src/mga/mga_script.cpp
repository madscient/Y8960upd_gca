// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "mga/mga_script.hpp"

#include <stdexcept>

namespace y8960upd::mga {

namespace {

constexpr std::uint8_t kReadIo = 0x03;
constexpr std::uint8_t kWriteIo = 0x04;
constexpr std::uint8_t kWait = 0x05;
constexpr std::uint8_t kCompare = 0x06;
constexpr std::uint8_t kAnd = 0x07;
constexpr std::uint8_t kJump = 0x0A;
constexpr std::uint8_t kPush = 0x0B;
constexpr std::uint8_t kAbort = 0xFE;
constexpr std::uint8_t kEnd = 0xFF;

}  // namespace

void MgaScript::read_io(std::uint16_t port) { emit(kReadIo, port, 0); }
void MgaScript::write_io(std::uint16_t port, std::uint8_t value) { emit(kWriteIo, port, value); }
void MgaScript::compare(std::uint8_t value) { emit(kCompare, 0, value); }
void MgaScript::and_(std::uint8_t mask) { emit(kAnd, 0, mask); }

void MgaScript::jump_back(int count)
{
    if (count < 1 || count > 128) throw std::invalid_argument("jump_back count out of range");
    emit(kJump, 0, static_cast<std::uint8_t>(256 - count));
}

void MgaScript::wait_us(std::uint16_t us) { emit(kWait, us, 0); }
void MgaScript::push(std::uint16_t address) { emit(kPush, address, 0); }
void MgaScript::end() { emit(kEnd, 0, 0); }
void MgaScript::abort() { emit(kAbort, 0, 0); }

void MgaScript::emit(std::uint8_t op, std::uint16_t address, std::uint8_t data)
{
    code_.push_back(op);
    code_.push_back(static_cast<std::uint8_t>(address >> 8));
    code_.push_back(static_cast<std::uint8_t>(address & 0xFF));
    code_.push_back(data);
}

}  // namespace y8960upd::mga
