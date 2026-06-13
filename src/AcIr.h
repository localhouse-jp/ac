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
#include <ir_Bosch.h>

// UI と内部で共有するエアコン状態
struct AcState {
  bool     power = true;
  uint8_t  mode  = kBosch144Cool;     // kBosch144 Cool/Dry/Auto/Heat/Fan
  uint8_t  temp  = 26;                // 16..30 (℃)
  uint16_t fan   = kBosch144FanAuto;  // kBosch144Fan20..100 / FanAuto
};

namespace AcIr {
// txGpio: IR LED, rxPin: 受信モジュール OUT
void begin(uint8_t txGpio, uint8_t rxPin);
// 状態を IR 送信 (RMT ハードウェアキャリア)
void send(const AcState& s);
// 受信した IR があれば out に反映して true (物理リモコン操作の取り込み用)
bool poll(AcState& out);
}  // namespace AcIr
