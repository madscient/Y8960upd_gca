# 設計

**この文書は、`y8960upd_gca` の中の構造（モジュールの分け方、層ごとの役目、試験の方式）を持つ。**
読者は、このリポジトリを実装する開発者と AI。原本とアダプタの仕様は `doc/spec.md`、
決めたことの理由と経緯は `doc/plan.md` にある。

## 層

下の層ほど Y8960 を知らない。上の層は下の層だけを使う。

| 層 | 置き場 | 役目 |
|---|---|---|
| シリアル | `src/serial/` | バイト列の送受信、タイムアウト、ポートの列挙。Win32 と POSIX の 2 つの実装 |
| MGA | `src/mga/` | コマンドの行の送受信、応答の終端の判定、開いた直後の同期、バイナリの送受信（`BRCV`・`BSND`）、スクリプトの組み立て |
| System Controller | `src/y8960/` | 検出、コマンドの実行と待ち、ConfigROM の読み出し・消去・書き込み |
| ファイル形式 | `src/image/` | ヘッダ付きの Config data / BIOS data と、生の BIOS（128KB）の判定と検査 |
| 更新の手順 | `src/updater/` | バージョンの表示、Config data・BIOS data の更新（消去 → 書き込み → ベリファイ → バージョンの更新） |
| コマンドライン | `src/main.cpp`、`src/cli/` | 引数の解析、表示、終了コード |

`src/` の下は、`main.cpp` を除いて静的ライブラリ `y8960upd_gca_core` にまとめ、実行ファイルと
試験の両方がそれをリンクする。

## シリアル

```cpp
class SerialPort {
public:
    virtual ~SerialPort() = default;
    virtual void write(std::span<const std::uint8_t> data) = 0;
    // 1 バイト以上届くか、timeout が過ぎるまで待つ。届いたバイト数を返す（0 はタイムアウト）
    virtual std::size_t read(std::span<std::uint8_t> buf, std::chrono::milliseconds timeout) = 0;
};
```

- 実装は `serial_win32.cpp`（`CreateFileW`・`SetCommState`・`SetCommTimeouts`）と
  `serial_posix.cpp`（termios）。どちらも 115200 8N1、DTR と RTS を立てる
  （Windows では `test/hw/` の試験がこの設定で動いた。Linux では mgadump が DTR を立てて動いている）
- break は送らない。送る関数を作らない
- ポートの列挙は VID:PID（`0b49:26d2`）で選ぶ。Windows は SetupAPI のハードウェア ID、
  Linux は `/sys/class/tty/*/device/../idVendor`。ほかの機器に文字列を送らずに済む

## MGA

- `Mga` は `SerialPort&` を受け取る。試験では偽の `SerialPort` を差す
- 応答は行単位で読み、終端は `OK`・`FAIL`・`Bad Parameter`・`Unknow Error`・
  `Unknown command: …` の 5 種類（mgadump の `porting-notes.md`）
- 開いた直後は、受信が 300ms 途切れるまで読み捨て、`HVER` を送って `OK` まで読み、もう一度
  読み捨てる（mgadump の `Mga::resync()` と同じ形）
- `BRCV` は、開始位置と長さの合計が `FFFFh` 以下であることを送る前に確かめる。範囲の外だと
  MGA はバイナリ受信に入らず、後に続くバイナリをコマンドとして読む（`doc/spec.md` の 6 章）
- `BSND` は、決まった長さのバイナリを読んでから、終端の行を読む
- `BSCR` の開始位置は常に `0000h`（開始位置の不具合を避ける。`doc/spec.md` の 6 章）
- スクリプトは `MgaScript` で組む。命令ごとの関数（`write_io`、`read_io`、`compare`、
  `and_`、`jump_back`、`wait_us`、`push`、`end`）を持ち、組み上がったバイト列を返す

## System Controller と ConfigROM

- `SystemController::open()`：`SPON`、`40h` の安定待ち、検出（`41h`・`42h`）。
  `close()`：`40h` に `00h`、`SPOFF`。`close()` はデストラクタからも呼ぶ。電源を入れる
  コマンドを送る前に、切る必要があることを記録する
- 電源を入れた後の待ちは、`40h` に `80h` を書いて `7Fh` が 20 回続けて読めるまで（約 1 秒）。
  BIOS のコピーの終わりとの関係は分かっていない（`doc/spec.md` の 6 章）
- `ConfigRom` の操作

  | 操作 | 方式 | 実機で |
  |---|---|---|
  | 読み出し | スクリプト。1 回で 1024 バイト。結果は PUSH で `C000h` から | 確かめた |
  | 64KB の消去 | PC から 1 往復ずつ。BUSY を一度も見なければ失敗 | 確かめた |
  | 書き込み | スクリプト。`FFh` は飛ばす。BUSY の待ちは 20µs の Wait（`40h` の待ちを入れない） | 確かめた |
  | ステータスの読み出し | PC から 1 往復ずつ | 確かめた |

- アドレスは、変わったバイトだけを `43h`〜`45h` に書く（レジスタは値を保つ。実機で確かめた）
- **書き込み・消去の許す範囲を `ConfigRom` が持つ。** 呼び出し側は「Config の領域」「バージョンの
  ブロック」「BIOS の領域」のどれかを名指しして書く。範囲の外のアドレスは、スクリプトを
  組む段階で例外にする（`test/hw/bios_block.ps1` と同じ考え）

## ファイル形式

| 種類 | 判定 | 本体 | バージョン |
|---|---|---|---|
| ヘッダ付きの Config data | 先頭 32 バイトが `Y8960_TP25K_CARTRIDGE` ＋ `00h` | 1 〜 `7D0000h` バイト | ヘッダの +32 |
| ヘッダ付きの BIOS data | 先頭 32 バイトが `Y8960_BIOS_IMAGE` ＋ `00h` | ちょうど `20000h` バイト | ヘッダの +32 |
| 生の BIOS | シグネチャが無く、ファイルがちょうど `20000h` バイト | ファイル全体 | オプションで指定。省略は 16 バイトの `00h` |

- ヘッダの無い Config は受け付けない（`doc/plan.md` の決めたこと 12）

## 更新の手順

原本（`doc/spec.md` の 3 章）と同じ順。

1. 対象の 64KB ブロックを順に消去し、ブロックごとに BUSY を見たことと、先頭 256 バイトが
   `FFh` であることを確かめる（原本と同じ。`doc/plan.md` の決めたこと 18）
2. 本体を書き、全体をベリファイする
3. ベリファイが通ったときだけ、バージョンのブロックを更新する（もう片方の 16 バイトを読み戻し、
   `7D0000h` を消去し、32 バイトを書いてベリファイ）

## コマンドライン

決めた理由は `doc/plan.md` の決めたこと 13〜16。

```
y8960upd_gca [--port PORT]                        バージョンを表示する
y8960upd_gca [--port PORT] -c CONFIG              Config data を書く
y8960upd_gca [--port PORT] -b BIOS [--bios-version STR]   BIOS data を書く
y8960upd_gca [--port PORT] -c CONFIG -b BIOS [--bios-version STR]
y8960upd_gca -h
```

- `--port` を省くと VID:PID で探す。0 台なら誤り、2 台以上なら一覧を表示して止まる
- `-b` のファイルは、ヘッダ付きの BIOS data か生の BIOS かを中身で判定する。`--bios-version` は
  生の BIOS のときだけ受け付け、ヘッダ付きに付けたら誤り。16 バイトを超えるか、表示できない文字を
  含めば誤り（`doc/plan.md` の決めたこと 17）
- 書き込みの前に、両方のファイルを読んで検査する。どちらかが誤りなら、カートリッジに触る前に止まる

## 試験

- 単体の試験：ファイル形式の判定、`MgaScript` の組み立て、行の読み取り
- 偽の MGA：`SerialPort` の偽物の中に、`test/model/sim_mga.py` と同じ振る舞い（スクリプトの
  実行、System Controller、Flash）を C++ で持つ。更新の手順を、実機なしで最後まで流す
- 偽物の期待値は、試験される側のコードではなく、ファイルの生のバイトから作る
- 試験の枠組みは Catch2（vcpkg）。`doc/plan.md` の決めたこと 5 の「必要になれば足す」に当たる
- 実機の試験は `test/hw/` の PowerShell で、手で走らせる。ctest には入れない
