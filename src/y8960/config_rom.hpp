// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#pragma once

#include "mga/mga.hpp"
#include "y8960/system_controller.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace y8960upd::y8960 {

inline constexpr std::uint32_t kRomSize = 0x800000;
inline constexpr std::uint32_t kBlockSize = 0x10000;
inline constexpr std::uint32_t kVersionAddress = 0x7DFF00;
inline constexpr std::size_t kVersionLength = 16;

// 書き込みと消去を許す範囲。呼び出し側はどれかを名指しする
enum class Region { Config, Version, Bios };

struct Range {
    std::uint32_t begin;
    std::uint32_t end;  // 含まない
};

constexpr Range range_of(Region r)
{
    switch (r) {
    case Region::Config: return {0x000000, 0x7D0000};
    case Region::Version: return {0x7D0000, 0x7E0000};
    case Region::Bios: return {0x7E0000, 0x800000};
    }
    return {0, 0};
}

// 原本の誤りの表示（spec の 5 章）に対応する
class RomError : public Y8960Error {
public:
    enum class Kind {
        ReadTimeout,       // ConfigROM read error.
        EraseTimeout,      // Erase error.
        EraseNotAccepted,  // Erase was not accepted (BUSY never set, SR1=xxh).
        WriteTimeout,      // Write error.
    };
    RomError(Kind kind, std::uint8_t status, const std::string& what) : Y8960Error(what), kind(kind), status(status) {}

    Kind kind;
    std::uint8_t status;
};

// SystemController::open() の後に使う
class ConfigRom {
public:
    using Progress = std::function<void(std::uint32_t address)>;

    // busy_timeout は消去の BUSY の待ちの上限。原本は 600 JIFFY（60Hz で 10 秒）
    explicit ConfigRom(mga::Mga& mga, std::chrono::milliseconds busy_timeout = std::chrono::seconds(10));

    std::vector<std::uint8_t> read(std::uint32_t address, std::size_t length, const Progress& progress = {});

    // ステータスレジスタ 1
    std::uint8_t status();

    // address を含む 64KB ブロックを消す。ブロックが region の外なら何も送らずに std::out_of_range
    void erase_block(Region region, std::uint32_t address);

    // FFh のバイトは書かない（消去の後は FFh なので）。範囲が region の外なら何も送らずに
    // std::out_of_range
    void write(Region region, std::uint32_t address, std::span<const std::uint8_t> data, const Progress& progress = {});

    // 直前の消去・書き込み・ステータスの読み出しで最後に読んだステータス
    std::uint8_t last_status() const { return last_status_; }

private:
    void wait_enabler(RomError::Kind on_timeout);
    void set_address(std::uint32_t address);
    void run(std::span<const std::uint8_t> script, RomError::Kind on_fail);

    mga::Mga& mga_;
    std::chrono::milliseconds busy_timeout_;
    std::uint8_t last_status_ = 0;
};

}  // namespace y8960upd::y8960
