// AcMqtt — Home Assistant 連携 (MQTT Climate 自動検出)
//
//  WiFi 接続後に begin()。loop() で非ブロッキング reconnect + 受信処理。
//  HA の MQTT discovery で climate エンティティを自動生成し、双方向同期する。
//  Apple Home へは HA の HomeKit Bridge で公開する (ESP 側は不要)。

#pragma once
#include <Arduino.h>
#include "AcIr.h"

namespace AcMqtt {
// state: 共有状態へのポインタ。onCommand: MQTT コマンドで state 更新後に呼ぶフック
// (主処理側で AcIr::send + publishState を行う)。
void begin(AcState* state, void (*onCommand)());
void loop();          // reconnect + mqtt.loop() (非ブロッキング)
void publishState();  // 現在の state を state topic へ retained publish
bool connected();
}  // namespace AcMqtt
