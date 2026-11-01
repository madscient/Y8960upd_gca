// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#pragma once

#include "serial/serial_port.hpp"
#include "y8960/system_controller.hpp"

#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace y8960upd::cli {

inline constexpr int kExitOk = 0;
inline constexpr int kExitDevice = 1;
inline constexpr int kExitUsage = 2;

// 試験では偽のポートを差す
struct Environment {
    std::function<std::vector<serial::PortInfo>()> find_ports = serial::find_mga_ports;
    std::function<std::unique_ptr<serial::SerialPort>(const std::string&)> open_port = serial::open_serial_port;
    y8960::PowerOnWait power_on_wait;
};

// args は argv[1] 以降。終了コードを返す
int run(const std::vector<std::string>& args, std::ostream& out, const Environment& env = {});

}  // namespace y8960upd::cli
