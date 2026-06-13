// AcIr — 東芝風(実体は Bosch144) エアコンの IR 送受信
//
//  送信: ESP32 の RMT で 38kHz をハードウェア生成 (C3 のソフトキャリアは不可)。
//        IRBosch144AC でバイト列を構築し、ベンダーバイトを実機の 0xC2 に差し替える。
//  受信: IRrecv で BOSCH144 をデコードし、温度/モード/風量/電源を取り出す。
//
//  ※ このリモコン(RG10J5(B3H)/BGJ)はライブラリ既定の Bosch(0xB2) ではなく
//    ベンダーバイト 0xC2 を使う。byte0 のみ差し替えればチェックサムは不変。

#pragma once
#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <ir_Bosch.h>

// UI と内部で共有するエアコン状態
struct AcState {
  bool     power = true;
  uint8_t  mode  = kBosch144Cool;     // kBosch144 Cool/Dry/Auto/Heat/Fan
  uint8_t  temp  = 26;                // 16..30 (℃)
  uint16_t fan   = kBosch144FanAuto;  // kBosch144Fan20..100 / FanAuto
};

namespace AcIr {
enum class PollResult : uint8_t {
  None = 0,
  Bosch144,
  NonBosch144,
};

struct RxDebugInfo {
  decode_type_t type = decode_type_t::UNKNOWN;
  uint16_t bits = 0;
  bool overflow = false;
};

// txGpio: IR LED, rxPin: 受信モジュール OUT
void begin(uint8_t txGpio, uint8_t rxPin);
// 送信の有効/無効 (受信単体切り分け用)
void setTxEnabled(bool enabled);
// 状態を IR 送信 (RMT ハードウェアキャリア)
void send(const AcState& s);
// 受信結果を返す。BOSCH144 の場合のみ out を更新。
PollResult poll(AcState& out, RxDebugInfo* dbg = nullptr);
}  // namespace AcIr
