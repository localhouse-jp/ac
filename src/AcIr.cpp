#include "AcIr.h"
#include "driver/rmt.h"
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

namespace {
const rmt_channel_t kCh = RMT_CHANNEL_0;
const uint8_t kVendorByte = 0xC2;   // 実機 RG10J5 のベンダーバイト (ライブラリ既定は 0xB2)
const uint8_t kRxTimeoutMs = 90;    // IRrecv timeout parameter is milliseconds.
const uint32_t kOffCoolix = 0xC27BE0;  // 電源OFF (Coolix。標準 0xB27BE0 のベンダー差)

IRBosch144AC encoder(0);  // バイト列構築専用 (RMT で送るので GPIO は未使用)
IRBosch144AC parser(0);   // 受信デコード結果の解釈用
IRrecv* irrecv = nullptr;
decode_results results;
uint32_t muteUntil = 0;   // 送信直後は自分の送信を受信無視 (至近の自己受信は化けるため)

void rmtWrite(rmt_item32_t* items, size_t n) { rmt_write_items(kCh, items, n, true); }

// 温度コード(bit0=半度を除いた整数部)から摂氏整数を引く
uint8_t celsiusFromCode(uint8_t code) {
  uint8_t base = code & 0x3E;  // bit0(TempS4/半度) を除く
  for (uint8_t i = 0; i < sizeof(kBosch144CelsiusMap); i++)
    if (kBosch144CelsiusMap[i] == base) return kBosch144CelsiusMin + i;
  return 25;
}

// Coolix 24bit を 48bit(各バイト+補数) に展開し RMT 送信 (電源OFF用)
void sendCoolixRmt(uint32_t data24) {
  uint64_t wire = 0;
  for (int i = 2; i >= 0; i--) {
    uint8_t b = (data24 >> (i * 8)) & 0xFF;
    wire = (wire << 8) | b;
    wire = (wire << 8) | (uint8_t)~b;
  }
  static rmt_item32_t items[64];
  size_t n = 0;
  auto push = [&](uint16_t m, uint16_t s) {
    items[n].level0 = 1; items[n].duration0 = m;
    items[n].level1 = 0; items[n].duration1 = s; n++;
  };
  push(4692, 4416);                              // Coolix ヘッダ
  for (int b = 47; b >= 0; b--)                  // 48bit MSB ファースト
    push(552, ((wire >> b) & 1) ? 1656 : 552);
  push(552, 5244);                               // フッタ + ギャップ
  rmtWrite(items, n);
  rmtWrite(items, n);                            // Coolix は2回送出
}

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

// ベンダー識別を実機の 0xC2 に差し替え。section1(byte0)とsection2(byte6)のみ。
// section3(byte12=固定0xD5, byte13=mode/fan)は touch しない (壊すとON拒否される)。
void patchVendor(uint8_t* raw, size_t nbytes) {
  raw[0] = kVendorByte; raw[1] = (uint8_t)~kVendorByte;
  if (nbytes > kBosch144BytesPerSection) {
    raw[6] = kVendorByte; raw[7] = (uint8_t)~kVendorByte;
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
  muteUntil = millis() + 500;  // 送信中〜直後の自己受信を無視
  if (!s.power) {            // 電源 OFF は Coolix
    sendCoolixRmt(kOffCoolix);
    return;
  }
  encoder.setPower(true);
  encoder.setMode(s.mode);
  encoder.setTemp(s.temp);   // 整数部 (ライブラリは整数のみ)
  encoder.setFan(s.fan);
  uint8_t* raw = encoder.getRaw();             // 18 バイト (3 セクション)
  if (s.half) {
    raw[14] |= 0x20;                           // TempS4 = 半度ビット
    raw[17] = raw[12] + raw[13] + raw[14] + raw[15] + raw[16];  // ChecksumS3 再計算
  }
  patchVendor(raw, kBosch144StateLength);      // ベンダーバイト 0xC2 (チェックサム非依存)
  encodeAndSend(raw, kBosch144StateLength);
}

bool poll(AcState& out) {
  if (!irrecv || !irrecv->decode(&results)) return false;
  if (millis() < muteUntil) {  // 送信直後の自己受信は破棄
    irrecv->resume();
    return false;
  }
  bool updated = false;
  if (results.decode_type == decode_type_t::COOLIX) {
    out.power = false;       // このリモコンの OFF は Coolix
    updated = true;
  } else if (results.decode_type == decode_type_t::BOSCH144 &&
             results.bits >= kBosch144Bits) {
    const uint8_t* st = results.state;
    parser.setRaw((uint8_t*)st, results.bits / 8);
    out.power = true;
    out.mode = parser.getMode();
    out.fan = parser.getFan();
    // 温度 (半度対応): code = (TempS1<<2)|(TempS3<<1)|TempS4
    uint8_t ts1 = (st[4] >> 4) & 0xF;
    uint8_t ts3 = (st[15] >> 4) & 1;
    uint8_t ts4 = (st[14] >> 5) & 1;
    uint8_t code = (ts1 << 2) | (ts3 << 1) | ts4;
    out.temp = celsiusFromCode(code);
    out.half = ts4;
    updated = true;
  }
  irrecv->resume();
  return updated;
}

}  // namespace AcIr
