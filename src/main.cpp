// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "cli/cli.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[])
{
    std::vector<std::string> args(argv + 1, argv + argc);
    static y8960upd::mga::Interrupt interrupt;
    y8960upd::cli::install_interrupt_handler(interrupt);
    y8960upd::cli::Environment env;
    env.interrupt = &interrupt;
    try {
        return y8960upd::cli::run(args, std::cout, env);
    } catch (const std::exception& e) {
        std::cout << std::flush;
        std::cerr << "\nInternal error: " << e.what() << "\n";
        return y8960upd::cli::kExitDevice;
    }
}
