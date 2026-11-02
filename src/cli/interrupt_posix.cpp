// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "cli/cli.hpp"

#include <csignal>
#include <cstdlib>

namespace y8960upd::cli {

namespace {

mga::Interrupt* g_interrupt = nullptr;

extern "C" void on_signal(int)
{
    if (g_interrupt->requested()) std::_Exit(kExitInterrupted);
    g_interrupt->request();
}

}  // namespace

void install_interrupt_handler(mga::Interrupt& interrupt)
{
    g_interrupt = &interrupt;
    struct sigaction sa{};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

}  // namespace y8960upd::cli
