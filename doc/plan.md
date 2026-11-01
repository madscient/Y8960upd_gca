# 作業計画

**この文書は、決めたこと（理由と前提）、見送った案、未決事項、残りの作業、経緯を持つ。**
読者は AI。経緯はこの文書にだけ書く。

## 目的

出所：`doc/initial_prompt.txt`（利用者による最初の依頼文）

- MGA を経由して、Y8960 Cartridge の FPGA とファームウェアを更新する PC 用ツールを作る
- 対象は Windows と Linux
- `../Y8960_Cartridge` の `Y8960UPD.COM`（MSX-DOS 用）と同等の機能を持つ。ただし ROM は
  生の `.rom` ファイルを受け付ける
- 実カートリッジとのインターフェースは `../MSXPLAYer_GameAdapter` を使う
- C++ / CMake / vcpkg

## 決めたこと

各項目に「理由」と「前提（これが変わらない限り成立する）」と「変えるときの値段」を書く。

### 1. 実行ファイル名は `y8960upd_gca`

- 理由：利用者の決定（2026-11-02）。原本の `Y8960UPD.COM`、Windows 版の `Y8960UpdWin.exe` と
  取り違えない
- 値段：`CMakeLists.txt` の `Y8960UPD_GCA_EXE_NAME` と、`README.md`・`doc/manual.md` の記述

### 2. 公開する。ライセンスは MIT、著作権表記は `MadScient`

- 理由：利用者の決定（2026-11-02）。原本の Y8960UPD とアダプタがどちらも MIT。著作権表記は
  利用者の別のリポジトリの `LICENSE` に揃えた
- ソースの冒頭は `SPDX-License-Identifier: MIT` と著作権表記の 2 行

### 3. `CLAUDE.md` は追跡し、手元に固有のことは `CLAUDE.local.md`（追跡外）に書く

- 理由：利用者の決定（2026-11-02）。初期の依頼文が `CLAUDE.md` をリポジトリの構成に含めている
- 前提：Claude Code が `CLAUDE.local.md` を自動で読む

### 4. C++20

- 理由：バイナリの扱い（`std::span` など）。MSVC と g++ の両方で使える
- 前提：Visual Studio 2022 以降、g++ 10 以降で組む
- 値段：`CMakeLists.txt` の 1 行

### 5. 依存ライブラリ無しで始める

- 理由：シリアル通信は、アダプタの公式ダンパー（Win32）と mgadump（termios）に実装と知見が
  あり、MGA に固有の同期（前回の残りの読み捨てなど）はどのライブラリを使っても自前で書く
- 前提：単体試験の枠組みや引数の解析で必要になれば、`vcpkg.json` に足す
- vcpkg の toolchain はプリセットで常に渡しておく（依存を足すときに手順が変わらない）

### 6. 改行は LF

- 理由：兄弟の mgadump と同じ。Windows 側と WSL 側の git から同じに見える

### 7. ビルド先は `build/<プリセット名>/`

- 理由：Windows と WSL で同じ作業ツリーを使ってもビルド先が混ざらない
- プリセットの構成は利用者の別のリポジトリに倣った

## 見送った案

- `CLAUDE.md` 全体を追跡の外にする（mgadump の形）。見送った理由：初期の依頼文が
  `CLAUDE.md` をリポジトリの構成に含めている。利用者が `CLAUDE.local.md` に分ける案を選んだ

## 未決事項

- 初期の依頼文の「FPGA、ファームウェア」と、原本の Config data / BIOS data の対応。
  「ファームウェア」が BIOS data を指すのかを利用者に確かめる
- 「生の .rom」がどのデータを指すか（BIOS data と見ているが未確認）。ヘッダが無いとき、
  バージョン情報文字列（16 バイト）をどこから取るか
- コマンドラインの書式（原本の `/C`・`/B` に倣うか、ポートの指定をどうするか）。外に出る値
- MGA のポートの見つけ方（VID:PID で選ぶか、HVER で探るか）
- 片方だけの更新を許すか。Windows 版は「両バージョンが同じ消去ブロックにあり、他方を読み
  出せない」ため常に両方を書く（`tool_for_windows/readme.md`）。原本の MSX 版は MSX のバス
  経由でバージョン領域を退避して片方を書く（`y8960upd.md`。asm では未確認）。同じバスを
  MGA で叩けば片方だけの更新ができる、は未確認の候補。MGA の IORD で ConfigROM を
  読み戻せるかを確かめれば決まる

## 残りの作業

1. 参照先のリポジトリを `git fetch` して `HEAD..origin/main` を見る
2. `y8960upd.asm` を読み、`doc/spec.md` の 1〜5 を書く（ConfigROM の配置、ファイル形式、
   動作、I/O ポートと Flash のコマンド、エラーの判定）
3. MGA で必要なコマンド（IORD / IOWR / IOTR / IOTW など）を `FIRMWARE/main.c` で確かめ、
   `doc/spec.md` の 6 を書く
4. 未決事項を利用者に聞く
5. モジュールの分割を決める（シリアル層の Win32 / POSIX、MGA のコマンド層、ConfigROM の
   操作、ファイル形式、CLI）
6. 試験の方式を決める。mgadump の偽の装置は pty を使うので Linux でしか走らない。
   Windows でも走るよう、通信路を抽象化してプロセス内の偽物を差す形を検討する
7. 実装と `doc/manual.md`

## 確かめたこと

- Windows：`windows-vs2026` と `windows-vs2022` で構成・ビルド・`ctest` が通る — 確認済み
  （2026-11-02、Visual Studio Community 2026 / 2022）
- Linux：`linux-system` のプリセットで構成・ビルド・`ctest` が通る — 確認済み
  （2026-11-02、WSL の Ubuntu、g++ 15.2、CMake 4.2.3）
- `linux-vcpkg` — 未検証（WSL に vcpkg が無い）
- 試験 `smoke_banner` は、出力の先頭を変えると落ちる — 確認済み（`main.cpp` の出力を一時的に
  変えて `ctest` が失敗することを見た）

## 経緯

- 2026-11-02 00:10 JST：スケルトンを作った。実行ファイル名・公開とライセンス・`CLAUDE.md` の
  扱いを利用者に聞いて決めた。実行ファイルは名前と版を表示するだけ
