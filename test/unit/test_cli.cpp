// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "cli/cli.hpp"
#include "fake/fake_mga.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using y8960upd::cli::Environment;
using y8960upd::cli::kExitDevice;
using y8960upd::cli::kExitOk;
using y8960upd::cli::kExitUsage;
using y8960upd::test::FakeMga;

namespace {

using namespace std::chrono_literals;
using namespace std::string_view_literals;

// 試験の側が持つ FakeMga へ読み書きを渡す
class PortRef final : public y8960upd::serial::SerialPort {
public:
    explicit PortRef(FakeMga& fake) : fake_(fake) {}
    void write(std::span<const std::uint8_t> data) override { fake_.write(data); }
    std::size_t read(std::span<std::uint8_t> buf, std::chrono::milliseconds timeout) override { return fake_.read(buf, timeout); }

private:
    FakeMga& fake_;
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

struct Rig {
    FakeMga fake;
    std::vector<y8960upd::serial::PortInfo> ports = {{"COM9", "USB\\VID_0B49&PID_26D2"}};
    int finds = 0;
    std::vector<std::string> opened;
    bool open_fails = false;
    std::ostringstream out;

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
            return std::make_unique<PortRef>(fake);
        };
        e.power_on_wait = {20, 0ms, 300};
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
        CHECK(r.run({"--port", "COM4"}) == kExitUsage);
        CHECK(r.out.str().ends_with("COM4 is not MGA.\n"));
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
