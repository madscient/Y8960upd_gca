// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#pragma once

#include "serial/serial_port.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace y8960upd::mga {

class MgaError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class Terminator { Ok, Fail, BadParameter, UnknownError, UnknownCommand };

struct Response {
    std::vector<std::string> lines;  // 終端の行を含まない
    Terminator end = Terminator::Fail;
    std::string end_line;

    bool ok() const { return end == Terminator::Ok; }
};

class Mga {
public:
    explicit Mga(serial::SerialPort& port, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    // 前回の実行の残りを読み捨て、HVER の応答で行の区切りを合わせる。読み捨てたバイト数を返す
    std::size_t resync();

    Response command(std::string_view line);
    // OK 以外なら MgaError
    Response command_ok(std::string_view line);

    std::uint8_t io_read(std::uint16_t port);
    void io_write(std::uint16_t port, std::uint8_t value);

    // BRCV。範囲の外なら何も送らずに MgaError
    void buffer_receive(std::uint16_t address, std::span<const std::uint8_t> data);
    // BSND
    std::vector<std::uint8_t> buffer_send(std::uint16_t address, std::size_t length);
    // BSCR,0。開始位置が 0 以外だと実行範囲が縮むので、常に 0 から実行する。
    // FAIL（待ちの回数の上限か Abort）なら false、ほかの終端なら MgaError
    bool run_script();

    static constexpr std::size_t kBufferSize = 0x10000;

private:
    void send_line(std::string_view line);
    std::string read_line();
    void read_exact(std::span<std::uint8_t> out);
    Response read_response();
    bool fill(std::chrono::milliseconds timeout);
    std::size_t drain(std::chrono::milliseconds quiet);

    serial::SerialPort& port_;
    std::chrono::milliseconds timeout_;
    std::vector<std::uint8_t> rx_;
    std::size_t rx_pos_ = 0;
};

}  // namespace y8960upd::mga
