// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#pragma once

#include <cerrno>
#include <chrono>

#include <poll.h>

namespace y8960upd::serial {

// poll はシグナルで EINTR を返して早く戻る。Ctrl+C で応答の待ちがタイムアウトに化けないよう、
// 期限まで待ち直す。戻り値と errno は poll と同じ（EINTR では戻らない）
inline int poll_until(int fd, short events, std::chrono::milliseconds timeout)
{
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + timeout;
    for (;;) {
        auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - clock::now());
        if (left.count() < 0) left = std::chrono::milliseconds(0);
        pollfd p{fd, events, 0};
        int r = ::poll(&p, 1, static_cast<int>(left.count()));
        if (r >= 0 || errno != EINTR) return r;
    }
}

}  // namespace y8960upd::serial
