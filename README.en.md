# y8960upd_gca

[日本語](README.md) | English

A command-line tool that updates the ConfigROM of the [Y8960 Cartridge](https://github.com/hra1129/Y8960_Cartridge)
from a PC through the [MSXPLAYer Game Cartridge Adapter](https://github.com/v9938/MSXPLAYer_GameAdapter) (MGA).

It performs the same update as `Y8960UPD.COM` for MSX-DOS, without an MSX computer.
In addition, a `.rom` file without the dedicated header (such as `y8960bas.rom` of
[MsxSoundSuiteExtension](https://github.com/madscient/MsxSoundSuiteExtension)) can be passed as the BIOS as is.

Config data and BIOS data with the dedicated header are accepted both compressed (such as the `Y8960CFG.BIN`
in the releases of the Y8960 Cartridge from v0.9.6 on) and uncompressed.

```
y8960upd_gca                                            Show the version information
y8960upd_gca -b y8960bas.rom --bios-version MSXSSE_v0.5.1   Write a BIOS without a header
y8960upd_gca -b MSXSSE.ROM                              Write BIOS data with a header
y8960upd_gca -c Y8960CFG.BIN                            Write Config data
```

## Requirements

- Windows 10/11 or Linux
- MGA
- Y8960 Cartridge

## Build

CMake 3.21 or later and a compiler that supports C++20 are required.
The dependencies are managed with the manifest of [vcpkg](https://vcpkg.io/) (`vcpkg.json`).

### Windows

Install Visual Studio 2022 or 2026 and vcpkg, and set the environment variable `VCPKG_ROOT` to the
vcpkg folder.

```sh
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026
ctest --preset windows-vs2026
```

For Visual Studio 2022, use the preset name `windows-vs2022`.
The executable is built as `build/<preset name>/Release/y8960upd_gca.exe`.

### Linux

Install g++ and CMake. The unit tests need Catch2 3 (`sudo apt install catch2` on Ubuntu).
Without Catch2 the tool is still built, but the unit tests are not.

```sh
cmake --preset linux-system
cmake --build --preset linux-system
ctest --preset linux-system
```

To use vcpkg, set `VCPKG_ROOT` and use the preset name `linux-vcpkg`.
The executable is built as `build/<preset name>/y8960upd_gca`.

## Usage

See [doc/manual.md](doc/manual.md) (in Japanese).

## License

MIT License. See [LICENSE](LICENSE).

The part that decompresses compressed files is ported from the decompressor of
[ZX0](https://github.com/einar-saukas/ZX0) by Einar Saukas and is under the BSD 3-Clause License.
See [LICENSE-zx0.txt](LICENSE-zx0.txt).
