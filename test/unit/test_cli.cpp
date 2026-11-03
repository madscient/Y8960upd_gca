// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "cli/cli.hpp"
#include "fake/fake_mga.hpp"
#include "unit/zx0_vector.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using y8960upd::cli::Environment;
using y8960upd::cli::kExitDevice;
using y8960upd::cli::kExitInterrupted;
using y8960upd::cli::kExitOk;
using y8960upd::cli::kExitUsage;
using y8960upd::test::FakeMga;

namespace {

using namespace std::chrono_literals;
using namespace std::string_view_literals;

// 試験の側が持つ FakeMga へ読み書きを渡す。書くたびに after_write を呼ぶ
class PortRef final : public y8960upd::serial::SerialPort {
public:
    PortRef(FakeMga& fake, std::function<void()> after_write) : fake_(fake), after_write_(std::move(after_write)) {}
    void write(std::span<const std::uint8_t> data) override
    {
        fake_.write(data);
        after_write_();
    }
    std::size_t read(std::span<std::uint8_t> buf, std::chrono::milliseconds timeout) override { return fake_.read(buf, timeout); }

private:
    FakeMga& fake_;
    std::function<void()> after_write_;
};

struct TempFile {
    std::filesystem::path path;
    TempFile(const std::string& name, const std::vector<std::uint8_t>& bytes)
        : path(std::filesystem::temp_directory_path() / ("y8960upd_gca_test_" + name))
    {
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    ~TempFile() { std::filesystem::remove(path); }
    std::string str() const { return path.string(); }
};

std::vector<std::uint8_t> body_of(std::size_t n)
{
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<std::uint8_t>(i * 7 + 1);
    return v;
}

std::vector<std::uint8_t> with_header(std::string_view signature, std::string_view version, const std::vector<std::uint8_t>& body)
{
    std::vector<std::uint8_t> f(48, 0);
    std::copy(signature.begin(), signature.end(), f.begin());
    std::copy(version.begin(), version.end(), f.begin() + 32);
    f.insert(f.end(), body.begin(), body.end());
    return f;
}

void push_le(std::vector<std::uint8_t>& f, std::uint32_t v, int bytes)
{
    for (int i = 0; i < bytes; ++i) f.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

// 圧縮した形。frames は長さのワードとデータの組
std::vector<std::uint8_t> compressed(std::string_view signature, std::string_view version, std::uint32_t original, std::uint16_t block,
                                     const std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>>& frames)
{
    auto f = with_header(signature, version, {});
    f[31] = 0x01;
    push_le(f, original, 4);
    push_le(f, block, 2);
    for (const auto& [word, data] : frames) {
        push_le(f, word, 2);
        f.insert(f.end(), data.begin(), data.end());
    }
    return f;
}

// どのブロックも、圧縮していない格納ブロックにする
std::vector<std::uint8_t> stored(std::string_view signature, std::string_view version, const std::vector<std::uint8_t>& body)
{
    std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>> frames;
    for (std::size_t i = 0; i < body.size(); i += 0x4000) {
        std::size_t n = std::min<std::size_t>(0x4000, body.size() - i);
        frames.push_back({static_cast<std::uint16_t>(0x8000 | n), {body.begin() + i, body.begin() + i + n}});
    }
    return compressed(signature, version, static_cast<std::uint32_t>(body.size()), 0x4000, frames);
}

const auto kVectorWord = static_cast<std::uint16_t>(y8960upd::test::kZx0Vector.size());

struct Rig {
    FakeMga fake;
    std::vector<y8960upd::serial::PortInfo> ports = {{"COM9", "USB\\VID_0B49&PID_26D2"}};
    int finds = 0;
    std::vector<std::string> opened;
    bool open_fails = false;
    std::ostringstream out;
    // Ctrl+C の代わり。偽物がこの条件を満たした時点で立てる
    y8960upd::mga::Interrupt stop;
    std::function<bool(const FakeMga&)> stop_when;
    std::size_t commands_at_stop = 0;

    Rig()
    {
        auto& f = fake.cart.flash;
        std::copy_n("Y8960_v0.9.3", 12, f.begin() + 0x7DFF00);
        std::fill_n(f.begin() + 0x7DFF0C, 4, std::uint8_t{0});
        std::copy_n("MSXSSE_v0.5.1", 13, f.begin() + 0x7DFF10);
        std::fill_n(f.begin() + 0x7DFF1D, 3, std::uint8_t{0});
    }

    Environment env()
    {
        Environment e;
        e.find_ports = [this] {
            ++finds;
            return ports;
        };
        e.open_port = [this](const std::string& name) -> std::unique_ptr<y8960upd::serial::SerialPort> {
            opened.push_back(name);
            if (open_fails) throw y8960upd::serial::SerialError("open " + name + " failed");
            return std::make_unique<PortRef>(fake, [this] {
                if (stop_when && !stop.requested() && stop_when(fake)) {
                    stop.request();
                    commands_at_stop = fake.commands.size();
                }
            });
        };
        e.power_on_wait = {20, 0ms, 300};
        e.interrupt = &stop;
        return e;
    }

    int run(std::vector<std::string> args) { return y8960upd::cli::run(args, out, env()); }

    bool touched() const { return finds > 0 || !opened.empty() || !fake.commands.empty(); }
    bool contains(std::string_view s) const { return out.str().find(s) != std::string::npos; }
};

const std::string kUsageHead = "Usage: y8960upd_gca ";

}  // namespace

TEST_CASE("-h shows the usage without looking for MGA")
{
    for (const char* h : {"-h", "-H", "-?"}) {
        Rig r;
        CHECK(r.run({h}) == kExitOk);
        CHECK(r.out.str().starts_with("y8960upd_gca "));
        CHECK(r.contains(kUsageHead));
        CHECK_FALSE(r.touched());
    }
}

TEST_CASE("a malformed command line shows the usage and touches nothing")
{
    const std::vector<std::vector<std::string>> bad = {
        {"-x"}, {"-c"}, {"-c", "a", "-c", "b"}, {"--bios-version", "v"}, {"--port"}, {"/C", "a"}, {"extra"},
    };
    for (const auto& args : bad) {
        Rig r;
        CHECK(r.run(args) == kExitUsage);
        CHECK(r.contains(kUsageHead));
        CHECK_FALSE(r.touched());
    }
}

TEST_CASE("no arguments show the usage and both versions, then power off")
{
    Rig r;
    CHECK(r.run({}) == kExitOk);
    CHECK(r.contains(kUsageHead));
    CHECK(r.out.str().ends_with("Cfg. Ver: Y8960_v0.9.3\nBIOS Ver: MSXSSE_v0.5.1\n"));
    CHECK(r.opened == std::vector<std::string>{"COM9"});
    CHECK_FALSE(r.fake.powered);
}

TEST_CASE("a headerless BIOS is written with the given version")
{
    Rig r;
    const auto body = body_of(0x20000);
    TempFile f("raw.rom", body);
    CHECK(r.run({"-b", f.str(), "--bios-version", "MSXSSE_v0.5.4"}) == kExitOk);
    CHECK(r.contains("File    : " + f.str() + "\nVersion : MSXSSE_v0.5.4\n"));
    CHECK(r.out.str().ends_with("Reset the MSX to load the new BIOS.\n"));
    CHECK(std::equal(body.begin(), body.end(), r.fake.cart.flash.begin() + 0x7E0000));
    CHECK(std::string(r.fake.cart.flash.begin() + 0x7DFF10, r.fake.cart.flash.begin() + 0x7DFF20) == std::string("MSXSSE_v0.5.4\0\0\0", 16));
    CHECK_FALSE(r.fake.powered);
}

TEST_CASE("a headerless BIOS without a version shows (none)")
{
    Rig r;
    TempFile f("raw_nover.rom", body_of(0x20000));
    CHECK(r.run({"-B", f.str()}) == kExitOk);
    CHECK(r.contains("Version : (none)\n"));
}

TEST_CASE("file errors stop before the cartridge is touched")
{
    TempFile raw("raw2.rom", body_of(0x20000));
    TempFile bios("bios.bin", with_header("Y8960_BIOS_IMAGE", "MSXSSE_v0.5.2", body_of(0x20000)));
    TempFile config("config.bin", with_header("Y8960_TP25K_CARTRIDGE", "Y8960_v0.9.5", body_of(0x100)));
    // ブロックの大きさを 2000 にすると、3000 バイトに展開されるストリームが合わない
    TempFile broken("broken.bin", compressed("Y8960_TP25K_CARTRIDGE", "", 3000, 2000, {{kVectorWord, y8960upd::test::kZx0Vector}}));
    auto cut_bytes = stored("Y8960_BIOS_IMAGE", "", body_of(0x20000));
    cut_bytes.pop_back();
    TempFile cut("cut.bin", cut_bytes);
    const std::string missing = (std::filesystem::temp_directory_path() / "y8960upd_gca_test_missing.bin").string();

    struct Case {
        std::vector<std::string> args;
        std::string message;
    };
    const std::vector<Case> cases = {
        {{"-b", missing}, "Cannot open the file.\n"},
        {{"-c", bios.str()}, "This file is not a valid image for this option.\n"},
        {{"-c", raw.str()}, "This file is not a valid image for this option.\n"},
        {{"-b", raw.str(), "--bios-version", "0123456789ABCDEFG"}, "The BIOS version string is longer than 16 characters.\n"},
        {{"-b", raw.str(), "--bios-version", "v\x7F"}, "The BIOS version string must be printable ASCII.\n"},
        {{"-b", bios.str(), "--bios-version", "v"}, "--bios-version cannot be used with a BIOS data file that has a header.\n"},
        {{"-c", broken.str()}, "Decompress error.\n"},
        {{"-b", cut.str()}, "File read error.\n"},
        // Config が正しくても、BIOS が誤りなら何も書かない
        {{"-c", config.str(), "-b", missing}, "Cannot open the file.\n"},
    };
    for (const auto& c : cases) {
        Rig r;
        CHECK(r.run(c.args) == kExitUsage);
        CHECK(r.out.str().ends_with(c.message));
        CHECK_FALSE(r.touched());
    }
}

TEST_CASE("Config and BIOS files are both shown before anything is written")
{
    Rig r;
    TempFile bios("bios2.bin", with_header("Y8960_BIOS_IMAGE", "MSXSSE_v0.5.2", body_of(0x20000)));
    TempFile config("config2.bin", with_header("Y8960_TP25K_CARTRIDGE", "Y8960_v0.9.5", body_of(0x100)));
    CHECK(r.run({"--port", "COM9", "-b", bios.str(), "-c", config.str()}) == kExitOk);
    CHECK(r.contains("File    : " + config.str() + "\nVersion : Y8960_v0.9.5\nFile    : " + bios.str() + "\nVersion : MSXSSE_v0.5.2\n\rErase   : 000000h"));
    CHECK(std::string(r.fake.cart.flash.begin() + 0x7DFF00, r.fake.cart.flash.begin() + 0x7DFF20) ==
          std::string("Y8960_v0.9.5\0\0\0\0MSXSSE_v0.5.2\0\0\0", 32));
}

TEST_CASE("compressed Config and BIOS files are written as their decompressed bodies")
{
    Rig r;
    const auto bios_body = body_of(0x20000);
    TempFile bios("bios_zx0.bin", stored("Y8960_BIOS_IMAGE", "MSXSSE_v0.5.4", bios_body));
    TempFile config("config_zx0.bin", compressed("Y8960_TP25K_CARTRIDGE", "Y8960_v0.9.7", 3000, 0x4000, {{kVectorWord, y8960upd::test::kZx0Vector}}));
    CHECK(r.run({"-c", config.str(), "-b", bios.str()}) == kExitOk);
    CHECK(r.contains("File    : " + config.str() + "\nVersion : Y8960_v0.9.7\nFile    : " + bios.str() + "\nVersion : MSXSSE_v0.5.4\n"));
    const auto config_body = y8960upd::test::zx0_vector_output();
    const auto& flash = r.fake.cart.flash;
    CHECK(std::equal(config_body.begin(), config_body.end(), flash.begin()));
    CHECK(flash[config_body.size()] == 0xFF);
    CHECK(std::equal(bios_body.begin(), bios_body.end(), flash.begin() + 0x7E0000));
    CHECK(std::string(flash.begin() + 0x7DFF00, flash.begin() + 0x7DFF20) == std::string("Y8960_v0.9.7\0\0\0\0MSXSSE_v0.5.4\0\0\0", 32));
}

TEST_CASE("the port must be one MGA found by its USB ID")
{
    {
        Rig r;
        r.ports.clear();
        CHECK(r.run({}) == kExitDevice);
        CHECK(r.out.str().ends_with("MGA is not found.\n"));
        CHECK(r.opened.empty());
    }
    {
        Rig r;
        r.ports.push_back({"COM10", "USB\\VID_0B49&PID_26D2"});
        CHECK(r.run({}) == kExitDevice);
        CHECK(r.out.str().ends_with("More than one MGA is found. Choose one with --port:\n  COM9\n  COM10\n"));
        CHECK(r.opened.empty());
    }
    {
        Rig r;
        r.ports.push_back({"COM10", "USB\\VID_0B49&PID_26D2"});
        CHECK(r.run({"--port", "COM10"}) == kExitOk);
        CHECK(r.opened == std::vector<std::string>{"COM10"});
    }
    {
        Rig r;
        CHECK(r.run({"--port", "COM8"}) == kExitUsage);
        CHECK(r.out.str().ends_with("COM8 is not MGA.\n"));
        CHECK(r.opened.empty());
    }
}

TEST_CASE("cartridge and communication errors end with exit code 1")
{
    {
        Rig r;
        r.fake.cartridge_inserted = false;
        CHECK(r.run({}) == kExitDevice);
        CHECK(r.out.str().ends_with("Y8960 Cartridge is not detected.\n"));
    }
    {
        Rig r;
        r.open_fails = true;
        CHECK(r.run({}) == kExitDevice);
        CHECK(r.out.str().ends_with("\nCommunication error: open COM9 failed\n"));
    }
    {
        Rig r;
        r.fake.cart.ignore_erase = true;
        TempFile f("raw3.rom", body_of(0x20000));
        CHECK(r.run({"-b", f.str()}) == kExitDevice);
        CHECK(r.contains("Erase was not accepted"));
        CHECK_FALSE(r.fake.powered);
    }
}

TEST_CASE("an interrupt while writing the BIOS powers off and leaves the versions alone")
{
    Rig r;
    const std::string versions(r.fake.cart.flash.begin() + 0x7DFF00, r.fake.cart.flash.begin() + 0x7DFF20);
    r.stop_when = [](const FakeMga& f) { return f.cart.writes > 0; };
    TempFile f("raw_stop.rom", body_of(0x20000));
    CHECK(r.run({"-b", f.str(), "--bios-version", "MSXSSE_v0.5.4"}) == kExitInterrupted);
    CHECK(r.out.str().ends_with("\nInterrupted.\n"));
    REQUIRE(r.commands_at_stop > 0);
    CHECK(std::vector<std::string>(r.fake.commands.begin() + static_cast<std::ptrdiff_t>(r.commands_at_stop), r.fake.commands.end()) ==
          std::vector<std::string>{"IOWR", "SPOFF"});
    CHECK_FALSE(r.fake.powered);
    CHECK(std::string(r.fake.cart.flash.begin() + 0x7DFF00, r.fake.cart.flash.begin() + 0x7DFF20) == versions);
}

TEST_CASE("an interrupt while the version block is rewritten waits until it is written back")
{
    Rig r;
    r.stop_when = [](const FakeMga& f) {
        return std::find(f.cart.erased_blocks.begin(), f.cart.erased_blocks.end(), 0x7D0000u) != f.cart.erased_blocks.end();
    };
    TempFile f("raw_stop2.rom", body_of(0x20000));
    CHECK(r.run({"-b", f.str(), "--bios-version", "MSXSSE_v0.5.4"}) == kExitOk);
    REQUIRE(r.commands_at_stop > 0);
    CHECK(r.out.str().ends_with("Reset the MSX to load the new BIOS.\n"));
    CHECK(std::string(r.fake.cart.flash.begin() + 0x7DFF00, r.fake.cart.flash.begin() + 0x7DFF20) ==
          std::string("Y8960_v0.9.3\0\0\0\0MSXSSE_v0.5.4\0\0\0", 32));
    CHECK_FALSE(r.fake.powered);
}

TEST_CASE("an interrupt before power on sends nothing more")
{
    Rig r;
    r.stop_when = [](const FakeMga& f) { return !f.commands.empty(); };
    CHECK(r.run({}) == kExitInterrupted);
    CHECK(r.out.str().ends_with("\nInterrupted.\n"));
    CHECK(r.fake.commands == std::vector<std::string>{"HVER"});
    CHECK_FALSE(r.fake.powered);
}
