// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace y8960upd::serial {

class SerialError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// break を送る操作は持たない。MGA の試作機は break で応答しなくなった
class SerialPort {
public:
    virtual ~SerialPort() = default;

    virtual void write(std::span<const std::uint8_t> data) = 0;

    // 1 バイト以上届くか、timeout が過ぎるまで待つ。タイムアウトなら 0 を返す
    virtual std::size_t read(std::span<std::uint8_t> buf, std::chrono::milliseconds timeout) = 0;
};

// 115200 8N1、DTR と RTS を立てて開く
std::unique_ptr<SerialPort> open_serial_port(const std::string& name);

struct PortInfo {
    std::string name;
    std::string hardware_id;
};

inline constexpr std::uint16_t kMgaVendorId = 0x0B49;
inline constexpr std::uint16_t kMgaProductId = 0x26D2;

// USB の VID:PID が MGA のポートを並べる。どのポートにも何も送らない
std::vector<PortInfo> find_mga_ports();

// 利用者が渡したポートの名前 given が、find_mga_ports() の返した名前 found と同じポートを指すか
bool same_port(const std::string& given, const std::string& found);

}  // namespace y8960upd::serial
