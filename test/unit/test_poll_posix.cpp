// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "serial/poll_posix.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <csignal>
#include <thread>

#include <pthread.h>
#include <unistd.h>

using namespace std::chrono_literals;

namespace {

extern "C" void ignore_signal(int) {}

}  // namespace

TEST_CASE("a signal during the wait does not cut the wait short")
{
    // SA_RESTART を付けないので、poll は EINTR で戻る
    struct sigaction sa{};
    struct sigaction old{};
    sa.sa_handler = ignore_signal;
    sigemptyset(&sa.sa_mask);
    REQUIRE(sigaction(SIGUSR1, &sa, &old) == 0);

    int fds[2];
    REQUIRE(pipe(fds) == 0);
    pthread_t waiter = pthread_self();
    std::thread other([&] {
        std::this_thread::sleep_for(100ms);
        pthread_kill(waiter, SIGUSR1);
        std::this_thread::sleep_for(200ms);
        char c = 'x';
        (void)!write(fds[1], &c, 1);
    });

    auto start = std::chrono::steady_clock::now();
    int r = y8960upd::serial::poll_until(fds[0], POLLIN, 3000ms);
    auto took = std::chrono::steady_clock::now() - start;
    other.join();

    CHECK(r == 1);
    CHECK(took >= 250ms);

    close(fds[0]);
    close(fds[1]);
    sigaction(SIGUSR1, &old, nullptr);
}

TEST_CASE("the wait still times out")
{
    int fds[2];
    REQUIRE(pipe(fds) == 0);
    auto start = std::chrono::steady_clock::now();
    CHECK(y8960upd::serial::poll_until(fds[0], POLLIN, 100ms) == 0);
    CHECK(std::chrono::steady_clock::now() - start >= 90ms);
    close(fds[0]);
    close(fds[1]);
}
