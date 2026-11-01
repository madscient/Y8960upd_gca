// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace y8960upd::mga {

// MGA のスクリプトモードの命令列。1 命令は [命令][アドレス上位][アドレス下位][データ] の 4 バイト
class MgaScript {
public:
    void read_io(std::uint16_t port);
    void write_io(std::uint16_t port, std::uint8_t value);
    // 直前の値が value と等しければ次の命令を飛ばす
    void compare(std::uint8_t value);
    // 直前の値と mask の AND が 0 なら次の命令を飛ばす
    void and_(std::uint8_t mask);
    // count 命令だけ前に戻る（1〜128）
    void jump_back(int count);
    // ファームは仕様書の「ms」ではなく µs として待つ（main.c の executeCommands）
    void wait_us(std::uint16_t us);
    // 直前の値をバッファの address に書く
    void push(std::uint16_t address);
    void end();
    void abort();

    std::span<const std::uint8_t> bytes() const { return code_; }
    std::size_t size() const { return code_.size(); }
    std::size_t instruction_count() const { return code_.size() / 4; }

private:
    void emit(std::uint8_t op, std::uint16_t address, std::uint8_t data);

    std::vector<std::uint8_t> code_;
};

}  // namespace y8960upd::mga
