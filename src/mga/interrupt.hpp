// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#pragma once

#include <atomic>
#include <stdexcept>

namespace y8960upd::mga {

// シグナルハンドラと Windows のコンソールのハンドラ（別のスレッド）から立てる
class Interrupt {
public:
    void request() noexcept { requested_.store(true); }
    bool requested() const noexcept { return requested_.load(); }

private:
    static_assert(std::atomic<bool>::is_always_lock_free, "request() must be async-signal-safe");
    std::atomic<bool> requested_{false};
};

// 通信の誤りとして扱われないように、MgaError とは別の系統にする
class InterruptedError : public std::runtime_error {
public:
    InterruptedError() : std::runtime_error("interrupted") {}
};

}  // namespace y8960upd::mga
