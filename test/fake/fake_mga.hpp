// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#pragma once

#include "serial/serial_port.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace y8960upd::test {

// Y8960 Cartridge の System Controller と ConfigROM の模型
class FakeCartridge {
public:
    FakeCartridge();

    void power_on();
    void io_write(std::uint16_t port, std::uint8_t value);
    std::uint8_t io_read(std::uint16_t port);

    std::vector<std::uint8_t> flash;
    int busy_reads_after_write = 2;
    int busy_reads_after_erase = 5;
    bool ignore_erase = false;

    int writes = 0;
    int writes_while_busy = 0;
    int erases = 0;
    std::vector<std::uint32_t> erased_blocks;

private:
    bool selected() const;
    std::uint32_t address() const;

    bool enabled_ = false;
    std::uint8_t devsel_ = 0;
    std::uint8_t func_ = 0;
    std::uint8_t addr_[3] = {};
    std::uint8_t data_in_ = 0;
    std::uint8_t data_out_ = 0;
    int busy_ = 0;
};

// MGA（ファーム 260927、main.c の bb58f7b）の振る舞いを写した偽物。write で受けたバイトを
// その場で処理し、応答を read で返す
class FakeMga final : public serial::SerialPort {
public:
    void write(std::span<const std::uint8_t> data) override;
    std::size_t read(std::span<std::uint8_t> buf, std::chrono::milliseconds timeout) override;

    // 開いた直後に届く、前回の実行の残り
    void preload(const std::string& bytes);

    FakeCartridge cart;
    bool cartridge_inserted = true;
    bool powered = false;
    int loop_max = 1000;
    std::vector<std::uint8_t> buffer = std::vector<std::uint8_t>(0x10000, 0);
    std::vector<std::string> commands;
    int scripts_run = 0;

private:
    void handle_line(const std::string& line, std::span<const std::uint8_t> rest, std::size_t& i);
    void execute(const std::string& cmd, const std::vector<int>& args);
    bool run_script(std::size_t start);
    void out(const std::string& s);

    std::deque<std::uint8_t> out_;
    std::string line_;
    bool crlf_flag_ = false;
    bool binary_ = false;
    char pending_line_end_ = 0;
    std::size_t bin_address_ = 0;
    std::size_t bin_left_ = 0;
};

}  // namespace y8960upd::test
