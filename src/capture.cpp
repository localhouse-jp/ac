// 受信キャプチャ (全プロトコル有効)。温度0.5刻み・OFF の生データ採取用。
//   pio run -e capture -t upload && pio device monitor
// 受信モジュール OUT -> GPIO5

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <IRac.h>

const uint16_t kRecvPin = 5;
IRrecv irrecv(kRecvPin, 1024, 90, true);
decode_results results;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(50);
  irrecv.enableIRIn();
  Serial.printf("\nキャプチャ開始 (GPIO%d)。リモコンを向けて押してください\n", kRecvPin);
}

void loop() {
  if (!irrecv.decode(&results)) return;
  Serial.printf("--- Protocol=%s bits=%d\n",
                typeToString(results.decode_type, results.repeat).c_str(), results.bits);
  // AC 系なら state バイト列を hex 表示
  if (hasACState(results.decode_type)) {
    Serial.print("state:");
    for (uint16_t i = 0; i < results.bits / 8; i++)
      Serial.printf(" %02X", results.state[i]);
    Serial.println();
  } else {
    Serial.printf("value: 0x%llX\n", (unsigned long long)results.value);
  }
  String desc = IRAcUtils::resultAcToString(&results);
  if (desc.length()) Serial.println(desc);
  irrecv.resume();
}
