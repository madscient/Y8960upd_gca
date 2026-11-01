// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#pragma once

#include "mga/mga.hpp"

#include <chrono>
#include <stdexcept>

namespace y8960upd::y8960 {

class Y8960Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// カートリッジが無いか、System Controller が応答しない
class NotDetectedError : public Y8960Error {
public:
    using Y8960Error::Y8960Error;
};

struct PowerOnWait {
    // 40h に 80h を書いて 7Fh が続けて読めた回数がこれに届いたら、起動が終わったとみなす。
    // BIOS のコピーの終わりとの関係は実機で分かっていない
    int stable_reads = 20;
    std::chrono::milliseconds interval{50};
    int max_reads = 300;
};

// SPON から SPOFF までを持つ。open() が成功したら、Y8960 Cartridge の Config ROM Reader/Writer が
// 選ばれている
class SystemController {
public:
    explicit SystemController(mga::Mga& mga, PowerOnWait wait = {});
    ~SystemController();
    SystemController(const SystemController&) = delete;
    SystemController& operator=(const SystemController&) = delete;

    // resync、LSCR、SCHK、SPON、起動の待ち、検出
    void open();
    // 40h に 00h、SPOFF。電源を入れていなければ何もしない。誤りは投げない
    void close() noexcept;

    mga::Mga& mga() { return mga_; }

private:
    void wait_power_on();
    void detect();

    mga::Mga& mga_;
    PowerOnWait wait_;
    bool must_power_off_ = false;
};

}  // namespace y8960upd::y8960
