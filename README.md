# y8960upd_gca

[MSXPLAYer Game Cartridge Adapter](https://github.com/v9938/MSXPLAYer_GameAdapter)（以下 MGA）を
経由して、PC から [Y8960 Cartridge](https://github.com/hra1129/Y8960_Cartridge) の
ConfigROM を更新するコマンドラインツールです。

MSX-DOS 用の更新ツール `Y8960UPD.COM` と同じ更新を、MSX 本体を使わずに行います。
加えて、BIOS には専用ヘッダの無い `.rom` ファイル（[MsxSoundSuiteExtension](https://github.com/madscient/MsxSoundSuiteExtension)
の `y8960bas.rom` など）をそのまま渡せます。

**開発中です。** 現在の版は起動して版を表示するだけで、カートリッジへの書き込みはできません。

## 必要なもの

- Windows 10/11 または Linux
- MGA
- Y8960 Cartridge

## ビルド

CMake 3.21 以上と、C++20 に対応したコンパイラが必要です。
依存ライブラリは [vcpkg](https://vcpkg.io/) のマニフェスト（`vcpkg.json`）で管理します。

### Windows

Visual Studio 2022 または 2026 と vcpkg を用意し、環境変数 `VCPKG_ROOT` に vcpkg の
フォルダを設定します。

```sh
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026
ctest --preset windows-vs2026
```

Visual Studio 2022 では、プリセット名を `windows-vs2022` にします。
実行ファイルは `build/<プリセット名>/Release/y8960upd_gca.exe` にできます。

### Linux

g++ と CMake を用意します。

```sh
cmake --preset linux-system
cmake --build --preset linux-system
ctest --preset linux-system
```

vcpkg を使う場合は、`VCPKG_ROOT` を設定してプリセット名を `linux-vcpkg` にします。
実行ファイルは `build/<プリセット名>/y8960upd_gca` にできます。

## 使い方

[doc/manual.md](doc/manual.md) を参照してください。

## ライセンス

MIT License です。[LICENSE](LICENSE) を参照してください。
