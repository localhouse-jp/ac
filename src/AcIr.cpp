#include "AcIr.h"
#include "driver/rmt.h"
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

namespace {
const rmt_channel_t kCh = RMT_CHANNEL_0;
const uint8_t kVendorByte = 0xC2;   // 実機 RG10J5 のベンダーバイト (ライブラリ既定は 0xB2)
const uint8_t kRxTimeoutMs = 90;    // IRrecv timeout parameter is milliseconds.

IRBosch144AC encoder(0);  // バイト列構築専用 (RMT で送るので GPIO は未使用)
IRBosch144AC parser(0);   // 受信デコード結果の解釈用
IRrecv* irrecv = nullptr;
decode_results results;

// 任意バイト列 (nbytes は 6 の倍数) を Bosch144 波形にして RMT 送信
void encodeAndSend(const uint8_t* bytes, size_t nbytes) {
  static rmt_item32_t items[160];
  size_t n = 0;
  auto push = [&](uint16_t mark, uint16_t space) {
    items[n].level0 = 1; items[n].duration0 = mark;   // mark = キャリア ON
    items[n].level1 = 0; items[n].duration1 = space;  // space = OFF
    n++;
  };
  for (size_t sec = 0; sec < nbytes / kBosch144BytesPerSection; sec++) {
    push(kBoschHdrMark, kBoschHdrSpace);
    for (uint8_t b = 0; b < kBosch144BytesPerSection; b++) {
      uint8_t byte = bytes[sec * kBosch144BytesPerSection + b];
      for (int8_t bit = 7; bit >= 0; bit--)  // MSB ファースト
        push(kBoschBitMark, ((byte >> bit) & 1) ? kBoschOneSpace : kBoschZeroSpace);
    }
    push(kBoschBitMark, kBoschFooterSpace);
  }
  rmt_write_items(kCh, items, n, true);
}

// 偶数バイトのベンダー識別を実機の 0xC2 に差し替え (奇数バイトはその反転)
void patchVendor(uint8_t* raw, size_t nbytes) {
  for (size_t i = 0; i + 1 < nbytes; i += kBosch144BytesPerSection) {
    raw[i] = kVendorByte;
    raw[i + 1] = (uint8_t)~kVendorByte;
  }
}
}  // namespace

namespace AcIr {

void begin(uint8_t txGpio, uint8_t rxPin) {
  rmt_config_t cfg = RMT_DEFAULT_CONFIG_TX((gpio_num_t)txGpio, kCh);
  cfg.clk_div = 80;  // 1 tick = 1us
  cfg.tx_config.carrier_en = true;
  cfg.tx_config.carrier_freq_hz = 38000;
  cfg.tx_config.carrier_duty_percent = 33;
  cfg.tx_config.carrier_level = RMT_CARRIER_LEVEL_HIGH;
  cfg.tx_config.idle_output_en = true;
  cfg.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  ESP_ERROR_CHECK(rmt_config(&cfg));
  ESP_ERROR_CHECK(rmt_driver_install(kCh, 0, 0));

  irrecv = new IRrecv(rxPin, 1024, kRxTimeoutMs, true);
  irrecv->enableIRIn();
}

void send(const AcState& s) {
  if (s.power) {
    encoder.setPower(true);
    encoder.setMode(s.mode);
    encoder.setTemp(s.temp);
    encoder.setFan(s.fan);
    uint8_t* raw = encoder.getRaw();          // 18 バイト (3 セクション)
    patchVendor(raw, kBosch144StateLength);
    encodeAndSend(raw, kBosch144StateLength);
  } else {
    // 電源 OFF は 96bit の専用メッセージ。ベンダーバイトだけ実機に合わせる。
    uint8_t off[sizeof(kBosch144Off)];
    memcpy(off, kBosch144Off, sizeof(off));
    patchVendor(off, sizeof(off));
    encodeAndSend(off, sizeof(off));
  }
}

bool poll(AcState& out) {
  if (!irrecv || !irrecv->decode(&results)) return false;
  bool updated = false;
  if (results.decode_type == decode_type_t::BOSCH144) {
    // 144bit(18B)=運転中, 96bit(12B)=電源OFF。電源はビット数で判定 (ベンダー非依存)。
    if (results.bits >= kBosch144Bits) {
      parser.setRaw(results.state, results.bits / 8);
      out.power = true;
      out.mode = parser.getMode();
      out.temp = parser.getTemp();
      out.fan = parser.getFan();
    } else {
      out.power = false;
    }
    updated = true;
  }
  irrecv->resume();
  return updated;
}

}  // namespace AcIr
