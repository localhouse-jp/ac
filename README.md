# hometoshibaac

ESP32-C3 + [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266) で、
東芝風エアコン（リモコン **RG10J5(B3H)/BGJ**）を Web ブラウザから操作する。

## ハマりどころ（重要）

- このリモコンは Toshiba ではなく **`BOSCH144` プロトコル**（電源OFFのみ `COOLIX`）。
- 送信キャリアは ESP32-C3 では **RMT ハードウェア生成が必須**（ソフトの digitalWrite だと 38kHz が歪んで効かない）。
- `IRBosch144AC` の既定ベンダーバイトは `0xB2` だが、**実機 RG10J5 は `0xC2`**。byte0 を差し替える（チェックサムは byte0 非依存なので安全）。OFF も Coolix `0xB27BE0`→`0xC27BE0`。
- **温度 0.5℃ 刻み**: ライブラリの整数マップ外。半度ビット `TempS4`(byte14 bit5) を別途扱う。
- **電源 OFF は別プロトコル**: BOSCH144 ではなく COOLIX `0xC27BE0`。送受信ともそれで処理。
- **自己受信ミュート**: 送信(GPIO4)を至近の受信機(GPIO5)が拾うと mode/fan が化けるため、送信後 500ms は受信を破棄。物理リモコンの受信には影響しない。

## 解析ツール

新しいリモコン/ボタンの生データを採取するには受信キャプチャ環境を使う:

```sh
pio run -e capture -t upload && pio device monitor   # Protocol/bits/state hex を表示
```

## 構成

| ファイル | 役割 |
|---------|------|
| `src/main.cpp` | WiFi / Web UI / MQTT・受信の状態同期 |
| `src/AcIr.{h,cpp}` | IR 送信(RMT 38kHz) + 受信デコード + 状態 |
| `src/AcMqtt.{h,cpp}` | Home Assistant 連携 (MQTT Climate 自動検出) |
| `src/secrets.h` | WiFi / MQTT 認証情報（**コミットしない**） |

## セットアップ

```sh
cp src/secrets.example.h src/secrets.h   # WiFi と MQTT(任意) の情報を記入
pio run -t upload
pio device monitor                        # シリアルで IP を確認
```

## Home Assistant / Apple Home 連携 (MQTT)

ESP が MQTT discovery を publish し、HA が `climate.toshiba_ac` を自動生成する。
Apple Home へは HA の **HomeKit Bridge** で公開する（ESP 側の追加実装は不要）。

1. **Mosquitto**: HA → Settings → Add-ons → Store →「Mosquitto broker」を Install/Start。
   ユーザを作成し、`src/secrets.h` の `MQTT_HOST/PORT/USER/PASS` に記入して再書き込み。
2. **MQTT 統合**: HA → Devices & Services → Add → MQTT（discovery 有効、prefix `homeassistant`）。
3. ESP 起動 → `climate.toshiba_ac` が自動生成される。
4. **Apple Home**: HA → Add → HomeKit。include に `climate` を含め、iOS「ホーム」でコードをスキャン。
   - 注: Apple Home は cool/heat/auto/off のみ。`dry`/`fan_only` は HA/Web からのみ操作可。

トピック: state `toshiba_ac/ac/state` / command `toshiba_ac/ac/set` / availability `toshiba_ac/availability`。
動作確認:
```sh
mosquitto_sub -h <HA_IP> -u <user> -P <pass> -v -t 'toshiba_ac/#'
mosquitto_pub -h <HA_IP> -u <user> -P <pass> -t toshiba_ac/ac/set -m '{"temp":27}'
```

> MQTT 未設定（ブローカー未到達）でも Web/IR は通常どおり動作する（接続は 2 秒タイムアウト・15 秒間隔で非同期リトライ）。

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
3. 物理リモコンを受信機に向けて操作すると Web 表示も同期する

## 受信が同期しないとき

`IR受信 -> 同期...` がシリアルに出ない場合は、まず受信機の OUT が **本当に GPIO5 に届いているか**を確認する（モジュールの LED は VCC/GND だけで光るので、点灯は OUT 接続の証明にならない）。送受信ピンは `main.cpp` の `kIrTxGpio` / `kIrRxPin` で変更可。
