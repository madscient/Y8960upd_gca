// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "y8960/config_rom.hpp"

#include "mga/mga_script.hpp"
#include "y8960/ports.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

namespace y8960upd::y8960 {

namespace {

constexpr std::size_t kReadChunk = 1024;
constexpr std::uint16_t kResultBase = 0xC000;
// 書き込みのスクリプトはこれを超えたら切る。読み出しの結果の置き場（kResultBase〜）に掛からない
constexpr std::size_t kScriptLimit = 0xBF00;
// 書き込みの BUSY の待ちで、ステータスのコマンドから 47h を読むまでの間。40h を待たない代わり
constexpr std::uint16_t kStatusWaitUs = 20;
// PC から 1 往復ずつ 40h を待つときの上限（1 往復は約 1ms）
constexpr int kEnablerPolls = 1000;

std::string hex6(std::uint32_t a) { return std::format("{:06X}h", a); }

bool contains(Region region, std::uint32_t address, std::size_t length)
{
    Range r = range_of(region);
    return address >= r.begin && address <= r.end && length <= r.end - address;
}

// ConfigROM を触るスクリプトを組む。アドレスのレジスタの値を追い、書き込みのコマンドを出す
// 時点のアドレスが allowed の外なら例外にする。消去のコマンドはスクリプトに入れない
class RomScript {
public:
    explicit RomScript(Range allowed) : allowed_(allowed) {}

    // 変わったバイトだけを書く。レジスタはコマンドを実行しても値を保つ（実機で確かめた）
    void address(std::uint32_t a)
    {
        put(0, kAddressLow, static_cast<std::uint8_t>(a));
        put(1, kAddressMid, static_cast<std::uint8_t>(a >> 8));
        put(2, kAddressHigh, static_cast<std::uint8_t>(a >> 16));
    }

    void data(std::uint8_t v) { s_.write_io(kData, v); }

    void command(std::uint8_t cmd)
    {
        if (cmd == kCmdWrite) {
            if (reg_[0] < 0 || reg_[1] < 0 || reg_[2] < 0) throw std::logic_error("script write before address is set");
            auto a = static_cast<std::uint32_t>(reg_[2] << 16 | reg_[1] << 8 | reg_[0]);
            if (a < allowed_.begin || a >= allowed_.end) throw std::out_of_range("script write outside the region at " + hex6(a));
        } else if (cmd != kCmdRead && cmd != kCmdStatus) {
            throw std::logic_error(std::format("command {:02X}h is not allowed in a script", cmd));
        }
        s_.write_io(kCommand, cmd);
    }

    // コマンドの後に 40h が 7Fh を返すまで待つ
    void wait_enabler()
    {
        s_.read_io(kEnabler);
        s_.compare(kEnablerReady);
        s_.jump_back(2);
    }

    // BUSY が解けるまで待つ。40h の待ちを挟まない。Compare が一致するとスクリプトの待ちの
    // 回数が 0 に戻るので、挟むと BUSY が解けないときに終わらなくなる（spec の 6 章）
    void wait_not_busy()
    {
        s_.write_io(kCommand, kCmdStatus);
        s_.wait_us(kStatusWaitUs);
        s_.read_io(kData);
        s_.and_(kStatusBusy);
        s_.jump_back(4);
    }

    void read_data_to(std::uint16_t buffer_address)
    {
        s_.read_io(kData);
        s_.push(buffer_address);
    }

    std::span<const std::uint8_t> finish()
    {
        s_.end();
        return s_.bytes();
    }

    std::size_t size() const { return s_.size(); }

private:
    void put(int i, std::uint16_t port, std::uint8_t v)
    {
        if (reg_[i] == v) return;
        s_.write_io(port, v);
        reg_[i] = v;
    }

    mga::MgaScript s_;
    Range allowed_;
    int reg_[3] = {-1, -1, -1};
};

}  // namespace

ConfigRom::ConfigRom(mga::Mga& mga, std::chrono::milliseconds busy_timeout) : mga_(mga), busy_timeout_(busy_timeout) {}

void ConfigRom::run(std::span<const std::uint8_t> script, RomError::Kind on_fail)
{
    mga_.buffer_receive(0, script);
    if (mga_.run_script()) return;
    std::uint8_t sr = 0;
    try {
        sr = status();
    } catch (const RomError&) {
    }
    throw RomError(on_fail, sr, "script stopped at its wait limit");
}

std::vector<std::uint8_t> ConfigRom::read(std::uint32_t address, std::size_t length, const Progress& progress)
{
    if (address > kRomSize || length > kRomSize - address) throw std::out_of_range("read outside the ConfigROM");
    std::vector<std::uint8_t> out;
    out.reserve(length);
    for (std::size_t off = 0; off < length; off += kReadChunk) {
        std::size_t n = std::min(kReadChunk, length - off);
        auto a = static_cast<std::uint32_t>(address + off);
        if (progress) progress(a);
        // 読み出しだけなので、書き込みを許す範囲は空にする
        RomScript s(Range{0, 0});
        for (std::size_t i = 0; i < n; ++i) {
            s.address(a + static_cast<std::uint32_t>(i));
            s.command(kCmdRead);
            s.wait_enabler();
            s.read_data_to(static_cast<std::uint16_t>(kResultBase + i));
        }
        run(s.finish(), RomError::Kind::ReadTimeout);
        auto got = mga_.buffer_send(kResultBase, n);
        out.insert(out.end(), got.begin(), got.end());
    }
    return out;
}

void ConfigRom::wait_enabler(RomError::Kind on_timeout)
{
    for (int i = 0; i < kEnablerPolls; ++i) {
        if (mga_.io_read(kEnabler) == kEnablerReady) return;
    }
    throw RomError(on_timeout, last_status_, "40h did not return 7Fh");
}

void ConfigRom::set_address(std::uint32_t address)
{
    mga_.io_write(kAddressLow, static_cast<std::uint8_t>(address));
    mga_.io_write(kAddressMid, static_cast<std::uint8_t>(address >> 8));
    mga_.io_write(kAddressHigh, static_cast<std::uint8_t>(address >> 16));
}

std::uint8_t ConfigRom::status()
{
    mga_.io_write(kCommand, kCmdStatus);
    wait_enabler(RomError::Kind::ReadTimeout);
    last_status_ = mga_.io_read(kData);
    return last_status_;
}

// PC から 1 往復ずつ待つ。スクリプトの待ちの上限（LSCR の 1000 回）に 20µs の Wait を掛けても
// 約 20ms で、64KB の消去（実機で約 104ms）に届かない ― 推測（Wait 以外の命令の時間を数えていない）
void ConfigRom::erase_block(Region region, std::uint32_t address)
{
    std::uint32_t block = address & ~(kBlockSize - 1);
    if (!contains(region, block, kBlockSize)) throw std::out_of_range("erase outside the region at " + hex6(block));

    set_address(block);
    mga_.io_write(kCommand, kCmdEraseBlock);
    wait_enabler(RomError::Kind::EraseTimeout);

    bool busy_seen = false;
    auto deadline = std::chrono::steady_clock::now() + busy_timeout_;
    for (;;) {
        mga_.io_write(kCommand, kCmdStatus);
        wait_enabler(RomError::Kind::EraseTimeout);
        last_status_ = mga_.io_read(kData);
        if ((last_status_ & kStatusBusy) == 0) break;
        busy_seen = true;
        if (std::chrono::steady_clock::now() > deadline) {
            throw RomError(RomError::Kind::EraseTimeout, last_status_, "BUSY did not clear at " + hex6(block));
        }
    }
    // 64KB の消去には数 ms 以上かかる。BUSY が見えなければ Flash がコマンドを無視した（原本と同じ判定）
    if (!busy_seen) {
        throw RomError(RomError::Kind::EraseNotAccepted, last_status_, "BUSY never set at " + hex6(block));
    }
}

void ConfigRom::write(Region region, std::uint32_t address, std::span<const std::uint8_t> data, const Progress& progress)
{
    if (!contains(region, address, data.size())) throw std::out_of_range("write outside the region at " + hex6(address));
    std::size_t i = 0;
    while (i < data.size()) {
        auto start = static_cast<std::uint32_t>(address + i);
        if (progress) progress(start);
        RomScript s(range_of(region));
        while (i < data.size() && s.size() < kScriptLimit) {
            if (data[i] != 0xFF) {
                s.address(static_cast<std::uint32_t>(address + i));
                s.data(data[i]);
                s.command(kCmdWrite);
                s.wait_enabler();
                s.wait_not_busy();
            }
            ++i;
        }
        // 残りが全部 FFh なら、空のスクリプトを送らない
        if (s.size() == 0) continue;
        run(s.finish(), RomError::Kind::WriteTimeout);
    }
    (void)status();
}

}  // namespace y8960upd::y8960
