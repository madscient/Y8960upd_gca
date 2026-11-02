# y8960upd_gca リファレンスマニュアル

MGA（MSXPLAYer Game Cartridge Adapter）を経由して、Y8960 Cartridge の ConfigROM を更新する
ツール `y8960upd_gca` の使い方です。

## 概要

Y8960 Cartridge の ConfigROM には、次の 3 つが入っています。

| 内容 | 説明 |
|---|---|
| Config data | FPGA の設定データ |
| BIOS data | MSX から見える BIOS（128KB） |
| バージョン情報 | Config data と BIOS data のバージョン情報文字列（それぞれ 16 文字まで） |

`y8960upd_gca` は、MSX-DOS 用の更新ツール `Y8960UPD.COM` と同じ手順で、これらを PC から
書き換えます。MSX 本体は使いません。

- 引数なしで実行すると、カートリッジに書かれているバージョン情報を表示します
- `-c` で Config data を、`-b` で BIOS data を書き込みます。両方を一度に指定できます
- BIOS data には、専用ヘッダの付いたファイルのほか、ヘッダの無い 128KB の `.rom` ファイル
  （MsxSoundSuiteExtension の `y8960bas.rom` など）をそのまま渡せます

## 準備

1. Y8960 Cartridge の基板上のスイッチ SW2 を ON（接続）にします。基板のリビジョンが
   Prototype Rev.1（基板のシルクに表記があります）なら、カートリッジの端子と反対の側に倒すと ON です。
   ほかのリビジョンでは、スイッチの位置や向きが違うことがあります
2. Y8960 Cartridge を MSX 本体から抜き、MGA の SLOT1 に挿します
3. MGA を USB で PC につなぎます
4. Linux では、シリアルポート（`/dev/ttyACM0` など）を開く権限が要ります

SW2 は、カートリッジをスロットから切り離すスイッチです。OFF のままだとカートリッジが
応答せず、`y8960upd_gca` は 15 秒ほど待ってから `Y8960 Cartridge is not detected.` で終わります。

カートリッジの電源は、`y8960upd_gca` が MGA を通して入れ、終わるときに切ります。

MGA は、USB の ID で自動的に探します。MGA を 2 台以上つないでいるときは、`--port` で
どちらを使うかを指定します。MGA ではない機器（ほかの USB シリアルなど）には何も送りません。

## コマンドライン

```
y8960upd_gca [--port PORT] [-c file] [-b file [--bios-version STR]]
y8960upd_gca -h
```

| オプション | 意味 |
|---|---|
| （なし） | 使い方と、Config data・BIOS data のバージョン情報を表示します |
| `-c file` | Config data を書き込みます |
| `-b file` | BIOS data を書き込みます |
| `--bios-version STR` | ヘッダの無い BIOS data のバージョン情報文字列を指定します。`-b` と一緒に使います |
| `--port PORT` | MGA のシリアルポートを指定します（Windows では `COM7`、Linux では `/dev/ttyACM0` など）。省くと自動で探します |
| `-h`、`-?` | 使い方を表示します。カートリッジには触りません |

- `-c`、`-b`、`-h` は大文字（`-C`、`-B`、`-H`）でも同じです
- 同じオプションを 2 回指定したり、知らない引数を渡したりすると、使い方を表示して終わります
- `-c` と `-b` を両方指定すると、Config data、BIOS data の順に書き込み、最後にバージョン情報を
  1 回だけ書き換えます

### `--bios-version`

ヘッダの無い BIOS data には、バージョン情報が入っていません。カートリッジに記録する文字列を
`--bios-version` で指定します。

- 16 文字までの ASCII の表示できる文字（空白から `~` まで）が使えます
- 省くと、BIOS data のバージョン情報は空になり、`(none)` と表示されます
- ヘッダ付きの BIOS data にはバージョン情報が入っているので、`--bios-version` は使えません

## ファイル形式

| 種類 | 形式 | 大きさ |
|---|---|---|
| Config data | 専用ヘッダ（48 バイト）＋本体 | 本体が 1 〜 8,192,000 バイト（`7D0000h`） |
| BIOS data（ヘッダ付き） | 専用ヘッダ（48 バイト）＋本体 | 本体がちょうど 131,072 バイト |
| BIOS data（ヘッダなし） | 本体だけ | ちょうど 131,072 バイト |

専用ヘッダは次の形です。Y8960 Cartridge のリポジトリの `tool/config/mk.bat` と
`tool/bios/mk.bat` で作るファイル（`Y8960CFG.BIN`、`MSXSSE.ROM`）がこの形です。

| 位置 | 長さ | 内容 |
|---|---|---|
| 0 | 32 | Config data は `Y8960_TP25K_CARTRIDGE`、BIOS data は `Y8960_BIOS_IMAGE`。残りは `00h` |
| 32 | 16 | バージョン情報文字列。16 文字より短ければ `00h` で終わる |
| 48 | — | 本体 |

- 種類はファイルの中身で見分けます。ファイル名や拡張子は見ません
- Config data は、ヘッダ付きのファイルだけを受け付けます
- `-b` に渡したファイルは、ヘッダがあればヘッダ付きの BIOS data、無くてちょうど 131,072 バイトなら
  ヘッダなしの BIOS data として扱います

## 動作

書き込みは、どちらも次の順に進みます。

1. 書き込む範囲を 64KB ずつ消去し、消えたことを確かめます
2. 16KB ずつ書き込み、読み戻して照合します
3. 照合がすべて通ったら、バージョン情報を書き換えます。書き換えない側のバージョン情報は、
   カートリッジから読んだ値をそのまま書き戻します

途中で失敗したときは、その先には進みません。バージョン情報も書き換えません。

ファイルに誤りがあるときは、カートリッジに触る前に止まります。`-c` と `-b` を両方指定したときは、
両方のファイルを検査してから書き込みを始めます。

BIOS data を書き込んだ後、新しい BIOS は、カートリッジを MSX に挿して起動したときから使われます。

### Ctrl+C で止めたとき

MGA に送っているコマンドの応答を待ってから止まり、カートリッジの電源を切って `Interrupted.` と
表示します。終了コードは 130 です。

- バージョン情報を書き換えている間（約 1 秒）は、書き換えが終わってから止まります
- もう一度 Ctrl+C を押すと、カートリッジの電源を切らずにすぐ終わります。MGA が応答しないときに使います
- Linux の SIGTERM と Windows の Ctrl+Break も、Ctrl+C と同じに扱います

### かかる時間

Windows での一例です。

| 操作 | 時間 |
|---|---|
| バージョン情報の表示 | 約 2 秒 |
| BIOS data の書き込み（128KB） | 約 30 秒 |

Config data の書き込みは、ファイルが大きいほど長くかかります。

## 使用例

バージョン情報を表示する：

```
y8960upd_gca
```

```
Cfg. Ver: Y8960_v0.9.3
BIOS Ver: MSXSSE_v0.5.1
```

MsxSoundSuiteExtension の `y8960bas.rom` を BIOS data として書き込む：

```
y8960upd_gca -b y8960bas.rom --bios-version MSXSSE_v0.5.1
```

```
File    : y8960bas.rom
Version : MSXSSE_v0.5.1
Erase   : 7F0000h OK
Write   : 800000h OK
Version update
Erase   : 7D0000h OK
 OK

BIOS data has been updated.
Reset the MSX to load the new BIOS.
```

`Erase` と `Write` の行は、進むにつれて同じ行の中でアドレスが書き換わります。

ヘッダ付きの Config data と BIOS data を一度に書き込み、MGA のポートを指定する：

```
y8960upd_gca --port COM7 -c Y8960CFG.BIN -b MSXSSE.ROM
```

## 終了コード

| 値 | 意味 |
|---|---|
| 0 | 成功 |
| 1 | MGA やカートリッジが見つからない、通信の誤り、消去・書き込み・照合の誤り |
| 2 | 引数の誤り、ファイルの誤り |
| 130 | Ctrl+C で止めた |

## エラーと対処

### ファイルと引数

| 表示 | 意味と対処 |
|---|---|
| `Cannot open the file.` | ファイルが開けません。パスを確かめてください |
| `File read error.` | ファイルを最後まで読めませんでした |
| `This file is not a valid image for this option.` | オプションに合う形式ではありません。`-c` に BIOS data を渡していないか、`-b` に 131,072 バイトでないヘッダなしのファイルを渡していないかを確かめてください |
| `Invalid data size.` | ヘッダ付きのファイルの本体の大きさが範囲の外です |
| `The BIOS version string is longer than 16 characters.` | `--bios-version` の文字列を 16 文字以内にしてください |
| `The BIOS version string must be printable ASCII.` | `--bios-version` に、ASCII の表示できる文字以外が含まれています |
| `--bios-version cannot be used with a BIOS data file that has a header.` | ヘッダ付きの BIOS data にはバージョン情報が入っているので、`--bios-version` を外してください |
| `<ポート> is not MGA.` | `--port` で指定したポートは MGA ではありません |

### MGA とカートリッジ

| 表示 | 意味と対処 |
|---|---|
| `MGA is not found.` | MGA が見つかりません。USB の接続を確かめてください |
| `More than one MGA is found. Choose one with --port:` | MGA が 2 台以上あります。続けて表示されるポートの 1 つを `--port` で指定してください |
| `Y8960 Cartridge is not detected.` | SLOT1 にカートリッジが無いか、カートリッジが応答しません。SW2 が ON になっていることを確かめ、挿し直してください |
| `Communication error: …` | MGA との通信に失敗しました。USB をつなぎ直してから、もう一度実行してください |
| `Interrupted.` | Ctrl+C で止めました（「動作」の「Ctrl+C で止めたとき」） |

### 消去・書き込み・照合

| 表示 | 意味 |
|---|---|
| `Erase error.` | 消去が時間内に終わりませんでした |
| `Erase was not accepted (BUSY never set, SR1=xxh).` | ConfigROM が消去を受け付けませんでした。書き込みが禁止されている可能性があります |
| `Erase check failed at aaaaaah: read xxh (last SR1=xxh).` | 消去した後に、消えていないバイトがありました |
| `Write error.` | 書き込みが時間内に終わりませんでした |
| `Verify error at aaaaaah: expected xxh, read xxh (last SR1=xxh).` | 書き込んだ内容を読み戻したら、違う値でした |
| `ConfigROM read error.` | ConfigROM を読めませんでした |

## 注意

- **書き込みの途中で止めないでください。** USB を抜いたり Ctrl+C で止めたりすると、
  書きかけの内容が残ります。止まってしまったときは、同じコマンドをもう一度実行してください
- バージョン情報を書き換えている途中で USB を抜いたり Ctrl+C を 2 回押したりして止まると、
  Config data と BIOS data の両方のバージョン情報が消えることがあります。本体は残ります
- **Config data を書き損じると、カートリッジの FPGA が起動しなくなり、MGA からは書き直せなく
  なります。** Config data の書き込みは、正しいファイルであることを確かめてから行ってください
