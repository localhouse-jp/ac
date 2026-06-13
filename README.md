# hometoshibaac

ESP32-C3 + [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266) で、
東芝風エアコン（リモコン **RG10J5(B3H)/BGJ**）を Web ブラウザから操作する。

## ハマりどころ（重要）

- このリモコンは Toshiba ではなく **`BOSCH144` プロトコル**。
- 送信キャリアは ESP32-C3 では **RMT ハードウェア生成が必須**（ソフトの digitalWrite だと 38kHz が歪んで効かない）。
- `IRBosch144AC` の既定ベンダーバイトは `0xB2` だが、**実機 RG10J5 は `0xC2`**。byte0 を差し替える（チェックサムは byte0 非依存なので安全）。

## 構成

| ファイル | 役割 |
|---------|------|
| `src/main.cpp` | WiFi / Web UI / 受信同期 |
| `src/AcIr.{h,cpp}` | IR 送信(RMT 38kHz) + 受信デコード + 状態 |
| `src/secrets.h` | WiFi 認証情報（**コミットしない**） |

## セットアップ

```sh
cp src/secrets.example.h src/secrets.h   # WiFi の SSID / PASS を記入
pio run -t upload
pio device monitor                        # シリアルで IP を確認
```

## 配線

| 部品 | ESP32-C3 |
|------|----------|
| IR LED（送信、トランジスタ駆動推奨） | GPIO4 |
| IR 受信モジュール OUT（VS1838B 等） | GPIO5 |
| VCC / GND | 3V3 / GND |

> IR LED は GPIO 直結だと暗い。NPN(2N2222 等) 駆動推奨：
> `GPIO4 ─[1kΩ]─ ベース / 5V ─[22〜33Ω]─ LED ─ コレクタ / エミッタ ─ GND`

## 使い方

1. シリアルに出る IP（または `http://toshiba-ac.local/`）にアクセス
2. 電源 / モード / 温度(16〜30℃) / 風量(自動・20〜100%) を操作 → IR 送信
3. 物理リモコンを受信機に向けて操作すると Web 表示も同期（受信機がクリーンに復調できる配線が前提）

## 受信トラブルの切り分け

1. `src/main.cpp` の `kIrTxEnabled` を `false` にして、まず受信単体で確認する
2. シリアルログで次を確認する
   - `IR受信(BOSCH144)` が出る: 受信デコード成功
   - `IR受信(非BOSCH144)` が出る: 受信はしているが復調/デコードが不安定
   - `IR受信なし(直近5秒)` が続く: 配線・電源・受光条件を優先して見直す
3. 受信が安定してから `kIrTxEnabled=true` に戻して送受信混在の確認へ進む
