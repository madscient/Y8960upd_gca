// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "mga/mga.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>

namespace y8960upd::mga {

namespace {

using namespace std::chrono_literals;

bool parse_terminator(const std::string& line, Terminator& t)
{
    if (line == "OK") t = Terminator::Ok;
    else if (line == "FAIL") t = Terminator::Fail;
    else if (line == "Bad Parameter") t = Terminator::BadParameter;
    // ファームの綴りのまま
    else if (line == "Unknow Error") t = Terminator::UnknownError;
    else if (line.starts_with("Unknown command:")) t = Terminator::UnknownCommand;
    else return false;
    return true;
}

std::string describe(std::string_view line, const Response& r)
{
    std::string s = std::format("{} -> {}", line, r.end_line);
    for (const auto& l : r.lines) s += " | " + l;
    return s;
}

}  // namespace

Mga::Mga(serial::SerialPort& port, std::chrono::milliseconds timeout) : port_(port), timeout_(timeout) {}

bool Mga::fill(std::chrono::milliseconds timeout)
{
    if (rx_pos_ > 0 && rx_pos_ == rx_.size()) {
        rx_.clear();
        rx_pos_ = 0;
    }
    std::array<std::uint8_t, 4096> tmp{};
    std::size_t n = port_.read(tmp, timeout);
    if (n == 0) return false;
    rx_.insert(rx_.end(), tmp.begin(), tmp.begin() + static_cast<std::ptrdiff_t>(n));
    return true;
}

std::size_t Mga::drain(std::chrono::milliseconds quiet)
{
    std::size_t n = rx_.size() - rx_pos_;
    rx_.clear();
    rx_pos_ = 0;
    auto give_up = std::chrono::steady_clock::now() + 30s;
    while (fill(quiet)) {
        n += rx_.size();
        rx_.clear();
        if (std::chrono::steady_clock::now() > give_up) throw MgaError("device keeps sending; cannot resync");
    }
    return n;
}

// 前回の実行を BSND の途中で止めると、装置の送信キューに残りが溜まり、開き直した後に届く。
// 残りの末尾の OK を HVER の応答と取り違えると、本物の応答はその後に届くので、読み捨てが
// 0 になるまで繰り返す（mgadump の Mga::resync() と同じ形）
std::size_t Mga::resync()
{
    std::size_t total = drain(300ms);
    for (int i = 0; i < 3; ++i) {
        send_line("HVER");
        (void)read_response();
        std::size_t extra = drain(300ms);
        total += extra;
        if (extra == 0) return total;
    }
    throw MgaError("cannot resync with MGA");
}

void Mga::check_interrupt() const
{
    if (interrupt_ && holds_ == 0 && interrupt_->requested()) throw InterruptedError();
}

void Mga::send_line(std::string_view line)
{
    check_interrupt();
    std::string s(line);
    s += "\r\n";
    port_.write(std::span(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
}

std::string Mga::read_line()
{
    for (;;) {
        auto begin = rx_.begin() + static_cast<std::ptrdiff_t>(rx_pos_);
        auto lf = std::find(begin, rx_.end(), static_cast<std::uint8_t>('\n'));
        if (lf != rx_.end()) {
            std::string line(begin, lf);
            rx_pos_ = static_cast<std::size_t>(lf - rx_.begin()) + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }
        if (!fill(timeout_)) throw MgaError("timed out waiting for a response line");
    }
}

void Mga::read_exact(std::span<std::uint8_t> out)
{
    std::size_t done = 0;
    while (done < out.size()) {
        std::size_t avail = rx_.size() - rx_pos_;
        if (avail == 0) {
            if (!fill(timeout_)) throw MgaError("timed out waiting for binary data");
            continue;
        }
        std::size_t n = std::min(avail, out.size() - done);
        std::copy_n(rx_.begin() + static_cast<std::ptrdiff_t>(rx_pos_), n, out.begin() + static_cast<std::ptrdiff_t>(done));
        rx_pos_ += n;
        done += n;
    }
}

Response Mga::read_response()
{
    Response r;
    for (;;) {
        std::string line = read_line();
        if (parse_terminator(line, r.end)) {
            r.end_line = line;
            return r;
        }
        r.lines.push_back(std::move(line));
    }
}

Response Mga::command(std::string_view line)
{
    send_line(line);
    return read_response();
}

Response Mga::command_ok(std::string_view line)
{
    Response r = command(line);
    if (!r.ok()) throw MgaError(describe(line, r));
    return r;
}

std::uint8_t Mga::io_read(std::uint16_t port)
{
    std::string line = std::format("IORD,{:X}", port);
    Response r = command_ok(line);
    // 応答は "%04x : %02x"
    if (r.lines.empty()) throw MgaError(describe(line, r));
    const std::string& s = r.lines.front();
    auto sep = s.find(" : ");
    if (sep == std::string::npos) throw MgaError(describe(line, r));
    unsigned value = 0;
    auto first = s.data() + sep + 3;
    auto last = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(first, last, value, 16);
    if (ec != std::errc{} || ptr != last || ptr - first != 2) throw MgaError(describe(line, r));
    return static_cast<std::uint8_t>(value);
}

void Mga::io_write(std::uint16_t port, std::uint8_t value)
{
    (void)command_ok(std::format("IOWR,{:X},{:X}", port, value));
}

// ファームは開始位置と長さの合計が 64KB に届くと受信モードに入らず、後に続くバイナリを
// コマンドとして読む（main.c の tud_cdc_rx_cb）。送る前にこちらで弾く
void Mga::buffer_receive(std::uint16_t address, std::span<const std::uint8_t> data)
{
    if (data.empty() || address + data.size() >= kBufferSize) {
        throw MgaError(std::format("BRCV range out of buffer: {:X}+{:X}", address, data.size()));
    }
    check_interrupt();
    std::string head = std::format("BRCV,{:X},{:X}\r\n", address, data.size());
    std::vector<std::uint8_t> packet(head.begin(), head.end());
    packet.insert(packet.end(), data.begin(), data.end());
    port_.write(packet);
    Response r = read_response();
    if (!r.ok()) throw MgaError(describe(head.substr(0, head.size() - 2), r));
}

std::vector<std::uint8_t> Mga::buffer_send(std::uint16_t address, std::size_t length)
{
    if (length == 0 || address + length > kBufferSize) {
        throw MgaError(std::format("BSND range out of buffer: {:X}+{:X}", address, length));
    }
    std::string line = std::format("BSND,{:X},{:X}", address, length);
    send_line(line);
    std::vector<std::uint8_t> data(length);
    read_exact(data);
    Response r = read_response();
    if (!r.ok()) throw MgaError(describe(line, r));
    return data;
}

bool Mga::run_script()
{
    constexpr std::string_view line = "BSCR,0";
    Response r = command(line);
    if (r.ok()) return true;
    if (r.end == Terminator::Fail) return false;
    throw MgaError(describe(line, r));
}

}  // namespace y8960upd::mga
