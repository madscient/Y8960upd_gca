// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "cli/cli.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>

namespace y8960upd::cli {

namespace {

mga::Interrupt* g_interrupt = nullptr;

// ウィンドウを閉じたときは、ハンドラから戻るとすぐに終了させられるので扱わない
BOOL WINAPI on_console_event(DWORD type)
{
    if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT) return FALSE;
    if (g_interrupt->requested()) std::_Exit(kExitInterrupted);
    g_interrupt->request();
    return TRUE;
}

}  // namespace

void install_interrupt_handler(mga::Interrupt& interrupt)
{
    g_interrupt = &interrupt;
    SetConsoleCtrlHandler(on_console_event, TRUE);
}

}  // namespace y8960upd::cli
