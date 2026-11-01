// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "fake/fake_mga.hpp"

#include <algorithm>
#include <cctype>
#include <format>

namespace y8960upd::test {

FakeCartridge::FakeCartridge() : flash(0x800000, 0xFF) {}

void FakeCartridge::power_on()
{
    enabled_ = false;
    devsel_ = 0;
    func_ = 0;
    busy_ = 0;
}

bool FakeCartridge::selected() const { return enabled_ && devsel_ == 0x02 && func_ == 0x01; }

std::uint32_t FakeCartridge::address() const
{
    return (static_cast<std::uint32_t>(addr_[2]) << 16 | static_cast<std::uint32_t>(addr_[1]) << 8 | addr_[0]) & 0x7FFFFF;
}

void FakeCartridge::io_write(std::uint16_t port, std::uint8_t value)
{
    switch (port & 0xFF) {
    case 0x40: enabled_ = value == 0x80; break;
    case 0x41: if (enabled_) devsel_ = value; break;
    case 0x42: if (enabled_ && devsel_ == 0x02) func_ = value; break;
    case 0x43: case 0x44: case 0x45: if (selected()) addr_[(port & 0xFF) - 0x43] = value; break;
    case 0x47: if (selected()) data_in_ = value; break;
    case 0x46:
        if (!selected()) break;
        switch (value) {
        case 0x00: data_out_ = flash[address()]; break;
        case 0x01:
            if (busy_ > 0) {
                ++writes_while_busy;
            } else {
                flash[address()] &= data_in_;
                ++writes;
                busy_ = busy_reads_after_write;
            }
            break;
        case 0x02:
            if (ignore_erase) break;
            {
                std::uint32_t block = address() & 0x7F0000;
                std::fill_n(flash.begin() + block, 0x10000, std::uint8_t{0xFF});
                ++erases;
                erased_blocks.push_back(block);
                busy_ = busy_reads_after_erase;
            }
            break;
        case 0x03:
            std::fill(flash.begin(), flash.end(), std::uint8_t{0xFF});
            busy_ = busy_reads_after_erase;
            break;
        case 0x04:
            data_out_ = busy_ > 0 ? 0x03 : 0x00;
            if (busy_ > 0) --busy_;
            break;
        default: break;
        }
        break;
    default: break;
    }
}

std::uint8_t FakeCartridge::io_read(std::uint16_t port)
{
    switch (port & 0xFF) {
    case 0x40: return enabled_ ? 0x7F : 0xFF;
    case 0x41: return enabled_ && devsel_ == 0x02 ? static_cast<std::uint8_t>(~devsel_) : 0xFF;
    case 0x42: return enabled_ && devsel_ == 0x02 ? (func_ == 0x01 ? 0xFE : 0xFF) : 0xFF;
    case 0x47: return selected() ? data_out_ : 0xFF;
    default: return 0xFF;
    }
}

namespace {

constexpr std::uint8_t kCr = 0x0D;
constexpr std::uint8_t kLf = 0x0A;
constexpr std::size_t kBuf = 0x10000;

int parse_hex(const std::string& s)
{
    if (s.empty()) return 0;
    std::size_t start = 0;
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        start = 2;
        if (s.size() == 2) return -1;
    }
    for (std::size_t i = start; i < s.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return -1;
    }
    return static_cast<int>(std::stoul(s.substr(start), nullptr, 16));
}

}  // namespace

void FakeMga::preload(const std::string& bytes) { out(bytes); }

void FakeMga::out(const std::string& s) { out_.insert(out_.end(), s.begin(), s.end()); }

std::size_t FakeMga::read(std::span<std::uint8_t> buf, std::chrono::milliseconds)
{
    std::size_t n = std::min(buf.size(), out_.size());
    for (std::size_t i = 0; i < n; ++i) {
        buf[i] = out_.front();
        out_.pop_front();
    }
    return n;
}

void FakeMga::write(std::span<const std::uint8_t> data)
{
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (binary_) {
            if (pending_line_end_ != 0) {
                char expected = pending_line_end_;
                pending_line_end_ = 0;
                if (data[i] == static_cast<std::uint8_t>(expected)) continue;
            }
            std::size_t n = std::min(data.size() - i, bin_left_);
            std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(i), n, buffer.begin() + static_cast<std::ptrdiff_t>(bin_address_));
            bin_address_ += n;
            bin_left_ -= n;
            i += n - 1;
            if (bin_left_ == 0) {
                binary_ = false;
                out("OK\n");
            }
            continue;
        }
        auto c = static_cast<std::uint8_t>(std::toupper(data[i]));
        if (c == kCr || c == kLf) {
            if (line_.empty()) {
                crlf_flag_ = true;
            } else {
                std::string line = line_;
                line_.clear();
                handle_line(line, data, i);
            }
        } else {
            line_.push_back(static_cast<char>(c));
        }
    }
}

void FakeMga::handle_line(const std::string& line, std::span<const std::uint8_t> rest, std::size_t& i)
{
    std::vector<std::string> tokens;
    std::size_t pos = 0;
    while (pos <= line.size() && tokens.size() < 5) {
        std::size_t comma = line.find(',', pos);
        std::string tok = line.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        // strtok は空のトークンを飛ばす
        if (!tok.empty()) tokens.push_back(tok);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    if (tokens.empty()) return;
    std::vector<int> args(4, -1);
    for (std::size_t k = 1; k < tokens.size() && k <= 4; ++k) args[k - 1] = parse_hex(tokens[k]);
    const std::string& cmd = tokens[0];

    if (cmd == "BRCV") {
        commands.push_back(line);
        std::size_t start = args[0] == -1 ? 0 : static_cast<std::size_t>(args[0]);
        std::size_t length = args[1] == -1 ? kBuf : static_cast<std::size_t>(args[1]);
        if (length >= kBuf) length = kBuf;
        bool fail = start >= kBuf || start + length >= kBuf;
        if (!fail) {
            binary_ = true;
            bin_address_ = start;
            bin_left_ = length;
            std::uint8_t opposite = rest[i] == kCr ? kLf : kCr;
            if (i + 1 < rest.size() && rest[i + 1] == opposite) {
                ++i;
                crlf_flag_ = true;
            } else if (crlf_flag_) {
                pending_line_end_ = static_cast<char>(opposite);
            }
            return;
        }
        out("FAIL\n");
        return;
    }
    execute(cmd, args);
}

void FakeMga::execute(const std::string& cmd, const std::vector<int>& args)
{
    commands.push_back(cmd);
    auto port_ok = [](int a) { return a >= 0 && a < 0x10000; };

    if (cmd == "HVER") {
        out("HARDWARE NAME : FAKE MGA\nHARDWARE VER : REV_B2\nFIRMWARE VER : 202609271002\nOK\n");
    } else if (cmd == "SCHK") {
        out(cartridge_inserted ? "0010\nOK\n" : "0000\nOK\n");
    } else if (cmd == "SPON") {
        powered = true;
        cart.power_on();
        out("OK\n");
    } else if (cmd == "SPOFF") {
        powered = false;
        out("OK\n");
    } else if (cmd == "LSCR") {
        loop_max = args[0] == -1 ? 1000 : args[0];
        out("OK\n");
    } else if (cmd == "IORD") {
        if (!port_ok(args[0]) || !powered) { out("FAIL\n"); return; }
        auto v = cart.io_read(static_cast<std::uint16_t>(args[0]));
        out(std::format("{:04x} : {:02x}\nOK\n", args[0], v));
    } else if (cmd == "IOWR") {
        if (!port_ok(args[0]) || args[1] == -1) { out("FAIL\n"); return; }
        out(std::format("{:04x} : {:02x}\n", args[0], args[1] & 0xFF));
        if (!powered) { out("FAIL\n"); return; }
        cart.io_write(static_cast<std::uint16_t>(args[0]), static_cast<std::uint8_t>(args[1]));
        out("OK\n");
    } else if (cmd == "BSND") {
        std::size_t start = args[0] == -1 ? 0 : static_cast<std::size_t>(args[0]);
        std::size_t length = args[1] == -1 ? kBuf : static_cast<std::size_t>(args[1]);
        if (start > kBuf || start + length > kBuf) { out("FAIL\n"); return; }
        out_.insert(out_.end(), buffer.begin() + static_cast<std::ptrdiff_t>(start), buffer.begin() + static_cast<std::ptrdiff_t>(start + length));
        out("OK\n");
    } else if (cmd == "BSCR") {
        std::size_t start = args[0] == -1 ? 0 : static_cast<std::size_t>(args[0]);
        ++scripts_run;
        out(run_script(start) ? "OK\n" : "FAIL\n");
    } else {
        out(std::format("Unknown command: [{}]\n", cmd));
    }
}

// main.c の executeCommands を写した。長さに kBuf - start を渡しながら絶対位置と比べる所も同じ
bool FakeMga::run_script(std::size_t start)
{
    std::size_t buf_size = kBuf - start;
    std::size_t i = start;
    int loop = 0;
    std::uint8_t last = 0;
    while (i < buf_size) {
        std::uint8_t op = buffer[i];
        std::uint16_t address = 0;
        std::uint8_t data = 0;
        if (i + 3 < buf_size) {
            address = static_cast<std::uint16_t>(buffer[i + 1] << 8 | buffer[i + 2]);
            data = buffer[i + 3];
        }
        switch (op) {
        case 0x00: break;
        case 0x03:
            if (!powered) return false;
            last = cart.io_read(address);
            break;
        case 0x04:
            if (!powered) return false;
            cart.io_write(address, data);
            last = data;
            break;
        case 0x05: break;
        case 0x06: case 0x07: case 0x08: case 0x09: {
            ++loop;
            bool skip = op == 0x06 ? last == data
                      : op == 0x07 ? (last & data) == 0
                      : op == 0x08 ? (last | data) == 0
                                   : (last ^ data) == 0;
            if (skip) {
                loop = 0;
                i += 4;
            }
            if (loop == loop_max) return false;
            break;
        }
        case 0x0A:
            if (data < 0x80) {
                i += static_cast<std::size_t>(data) * 4;
            } else {
                long back = (static_cast<long>(data) - 256) * 4;
                if (static_cast<long>(i) + back < 0) return false;
                i = static_cast<std::size_t>(static_cast<long>(i) + back);
            }
            i -= 4;
            break;
        case 0x0B: buffer[address] = last; break;
        case 0xFE: return false;
        case 0xFF: return true;
        default: return false;
        }
        i += 4;
    }
    return true;
}

}  // namespace y8960upd::test
