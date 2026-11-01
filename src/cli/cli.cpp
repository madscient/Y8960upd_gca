// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "cli/cli.hpp"

#include "image/image.hpp"
#include "mga/mga.hpp"
#include "updater/updater.hpp"
#include "y8960/config_rom.hpp"

#include <optional>

namespace y8960upd::cli {

namespace {

constexpr const char* kUsage =
    "Usage: " Y8960UPD_GCA_NAME " [--port PORT] [-c file] [-b file [--bios-version STR]]\n"
    "       " Y8960UPD_GCA_NAME " -h\n"
    "  (none)              Show the version information.\n"
    "  -c file             Write the Config data.\n"
    "  -b file             Write the BIOS data (with a header, or a 128KB body without one).\n"
    "  --bios-version STR  Version string for a BIOS body without a header (up to 16 characters).\n"
    "  --port PORT         Serial port of MGA. Found by its USB ID when omitted.\n"
    "  -h, -?              Show this help.\n"
    "\n";

struct Options {
    bool help = false;
    std::optional<std::string> config;
    std::optional<std::string> bios;
    std::optional<std::string> bios_version;
    std::optional<std::string> port;
};

// 原本と同じく、1 文字のオプションは大文字も受け付ける
std::optional<Options> parse(const std::vector<std::string>& args)
{
    Options o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        std::optional<std::string>* slot = nullptr;
        if (a == "-h" || a == "-H" || a == "-?") {
            o.help = true;
            continue;
        }
        if (a == "-c" || a == "-C") slot = &o.config;
        else if (a == "-b" || a == "-B") slot = &o.bios;
        else if (a == "--bios-version") slot = &o.bios_version;
        else if (a == "--port") slot = &o.port;
        else return std::nullopt;
        if (slot->has_value() || i + 1 >= args.size()) return std::nullopt;
        *slot = args[++i];
    }
    if (o.bios_version && !o.bios) return std::nullopt;
    return o;
}

const char* message_of(image::ImageError::Reason r)
{
    using R = image::ImageError::Reason;
    switch (r) {
    case R::Open: return "Cannot open the file.";
    case R::Read: return "File read error.";
    case R::Format: return "This file is not a valid image for this option.";
    case R::Size: return "Invalid data size.";
    case R::BiosVersionTooLong: return "The BIOS version string is longer than 16 characters.";
    case R::BiosVersionNotPrintable: return "The BIOS version string must be printable ASCII.";
    case R::BiosVersionWithHeader: return "--bios-version cannot be used with a BIOS data file that has a header.";
    }
    return "";
}

template <class Parse>
std::optional<image::Image> load(const std::string& name, Parse&& parse_file, std::ostream& out)
{
    out << "File    : " << name << "\n";
    try {
        image::Image img = parse_file(image::read_file(name));
        out << "Version : " << image::display_version(img.version) << "\n";
        return img;
    } catch (const image::ImageError& e) {
        out << message_of(e.reason) << "\n";
        return std::nullopt;
    }
}

// ほかの機器には何も送らない。--port の名前も、VID:PID で見つけたポートの中にあるときだけ使う
std::optional<std::string> choose_port(const std::optional<std::string>& given, const Environment& env, std::ostream& out, int& code)
{
    auto ports = env.find_ports();
    if (given) {
        for (const auto& p : ports) {
            if (serial::same_port(*given, p.name)) return p.name;
        }
        out << *given << " is not MGA.\n";
        code = kExitUsage;
        return std::nullopt;
    }
    if (ports.size() == 1) return ports.front().name;
    code = kExitDevice;
    if (ports.empty()) {
        out << "MGA is not found.\n";
    } else {
        out << "More than one MGA is found. Choose one with --port:\n";
        for (const auto& p : ports) out << "  " << p.name << "\n";
    }
    return std::nullopt;
}

}  // namespace

int run(const std::vector<std::string>& args, std::ostream& out, const Environment& env)
{
    out << Y8960UPD_GCA_NAME " " Y8960UPD_GCA_VERSION " [Y8960 Cartridge Update Tool for MSXPLAYer GameAdapter]\n\n";
    auto opt = parse(args);
    if (!opt) {
        out << kUsage;
        return kExitUsage;
    }
    if (opt->help) {
        out << kUsage;
        return kExitOk;
    }

    // カートリッジに触る前に、両方のファイルを検査する
    updater::Request request;
    if (opt->config) {
        request.config = load(*opt->config, [](const image::FileContent& f) { return image::parse_config(f); }, out);
        if (!request.config) return kExitUsage;
    }
    if (opt->bios) {
        const auto& version = opt->bios_version;
        request.bios = load(*opt->bios, [&](const image::FileContent& f) { return image::parse_bios(f, version); }, out);
        if (!request.bios) return kExitUsage;
    }
    bool info = !request.config && !request.bios;
    // 原本は引数なしで使い方とバージョンの両方を出す
    if (info) out << kUsage;

    int code = kExitOk;
    try {
        auto port_name = choose_port(opt->port, env, out, code);
        if (!port_name) return code;
        auto port = env.open_port(*port_name);
        mga::Mga mga(*port);
        y8960::SystemController sc(mga, env.power_on_wait);
        sc.open();
        y8960::ConfigRom rom(mga);
        bool ok = info ? updater::show_versions(rom, out) : updater::update(rom, request, out);
        return ok ? kExitOk : kExitDevice;
    } catch (const y8960::NotDetectedError&) {
        out << "Y8960 Cartridge is not detected.\n";
    } catch (const mga::MgaError& e) {
        out << "\nCommunication error: " << e.what() << "\n";
    } catch (const serial::SerialError& e) {
        out << "\nCommunication error: " << e.what() << "\n";
    }
    return kExitDevice;
}

}  // namespace y8960upd::cli
