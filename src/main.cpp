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
    try {
        return y8960upd::cli::run(args, std::cout);
    } catch (const std::exception& e) {
        std::cout << std::flush;
        std::cerr << "\nInternal error: " << e.what() << "\n";
        return y8960upd::cli::kExitDevice;
    }
}
