// ESP32-C3 エアコン Web リモコン (Bosch144 / RG10J5 系)
//
//  - WiFi 接続して Web UI を提供 (電源/モード/温度/風量)
//  - 送信は RMT ハードウェアキャリア (AcIr モジュール)
//  - 物理リモコンの操作を受信して Web 表示に同期
//
//  配線:  IR LED -> GPIO4 (送信)   受信モジュール OUT -> GPIO5

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "AcIr.h"
#include "secrets.h"  // WIFI_SSID / WIFI_PASS (コミットしない)

// ---- 設定 ----
const char* kHostname = "toshiba-ac";  // http://toshiba-ac.local/
const uint8_t kIrTxGpio = 4;
const uint8_t kIrRxPin = 5;
const bool kIrTxEnabled = true;        // 受信単体切り分け時は false にする

WebServer server(80);
AcState ac;  // 現在の状態 (Web と受信で共有)

// UI の風量 1..5 と Bosch144 ファンコードの対応
const uint16_t kFanSteps[] = {kBosch144Fan20, kBosch144Fan40, kBosch144Fan60,
                              kBosch144Fan80, kBosch144Fan100};

String modeName(uint8_t m) {
  switch (m) {
    case kBosch144Auto: return "自動";
    case kBosch144Cool: return "冷房";
    case kBosch144Dry:  return "除湿";
    case kBosch144Heat: return "暖房";
    case kBosch144Fan:  return "送風";
    default:            return "?";
  }
}

String fanName(uint16_t f) {
  if (f == kBosch144FanAuto || f == kBosch144FanAuto0) return "自動";
  for (uint8_t i = 0; i < 5; i++)
    if (f == kFanSteps[i]) return String((i + 1) * 20) + "%";
  return "自動";
}

// ---- HTML ----
String htmlPage() {
  String h = F("<!DOCTYPE html><html lang='ja'><head><meta charset='utf-8'>"
               "<meta name='viewport' content='width=device-width,initial-scale=1'>"
               "<title>東芝エアコン リモコン</title><style>"
               "body{font-family:sans-serif;max-width:420px;margin:0 auto;padding:16px;background:#0f172a;color:#e2e8f0}"
               "h1{font-size:1.3rem}.card{background:#1e293b;border-radius:12px;padding:16px;margin-bottom:14px}"
               ".row{display:flex;align-items:center;justify-content:space-between;margin:10px 0}"
               "a.btn{display:inline-block;background:#334155;color:#e2e8f0;border:none;border-radius:8px;"
               "padding:10px 14px;font-size:1rem;text-decoration:none;cursor:pointer;margin:2px}"
               "a.btn.on{background:#2563eb}.big{font-size:2rem;font-weight:bold}"
               ".pw{background:#16a34a}.po{background:#dc2626}</style></head><body>");
  h += F("<h1>東芝エアコン リモコン</h1>");

  // 電源
  h += "<div class='card'><div class='row'><span>電源</span><span class='big'>";
  h += ac.power ? "ON" : "OFF";
  h += "</span></div><div class='row'>";
  h += "<a class='btn pw' href='/set?power=1'>ON</a>";
  h += "<a class='btn po' href='/set?power=0'>OFF</a></div></div>";

  // 運転モード
  h += "<div class='card'><div class='row'><span>運転モード</span><b>" + modeName(ac.mode) + "</b></div><div>";
  uint8_t modes[] = {kBosch144Auto, kBosch144Cool, kBosch144Dry, kBosch144Heat, kBosch144Fan};
  for (uint8_t m : modes)
    h += "<a class='btn" + String(ac.mode == m ? " on" : "") + "' href='/set?mode=" + String(m) + "'>" + modeName(m) + "</a>";
  h += "</div></div>";

  // 温度
  h += "<div class='card'><div class='row'><span>温度</span><span class='big'>" + String(ac.temp) + "&deg;C</span></div><div class='row'>";
  h += "<a class='btn' href='/set?temp=" + String(ac.temp - 1) + "'>&minus;</a>";
  h += "<a class='btn' href='/set?temp=" + String(ac.temp + 1) + "'>&plus;</a></div></div>";

  // 風量 (fan=0:自動, 1..5:20〜100%)
  h += "<div class='card'><div class='row'><span>風量</span><b>" + fanName(ac.fan) + "</b></div><div>";
  for (uint8_t i = 0; i <= 5; i++) {
    uint16_t fcode = (i == 0) ? kBosch144FanAuto : kFanSteps[i - 1];
    h += "<a class='btn" + String(ac.fan == fcode ? " on" : "") + "' href='/set?fan=" + String(i) + "'>" + fanName(fcode) + "</a>";
  }
  h += "</div></div>";

  h += "<p style='opacity:.6;font-size:.8rem'>操作で IR 送信。物理リモコンの操作も反映されます。</p>";
  // 受信同期: /state を監視し、状態が変わったら再読み込み
  h += F("<script>let last='';setInterval(async()=>{try{"
         "const t=await (await fetch('/state')).text();"
         "if(last&&t!==last)location.reload();last=t;}catch(e){}},1500);</script>");
  h += "</body></html>";
  return h;
}

// 現在状態の短い署名 (変化検知用)
String stateSig() {
  return String(ac.power) + ":" + ac.mode + ":" + ac.temp + ":" + ac.fan;
}

void handleRoot() { server.send(200, "text/html; charset=utf-8", htmlPage()); }
void handleState() { server.send(200, "text/plain", stateSig()); }

void handleSet() {
  if (server.hasArg("power")) ac.power = server.arg("power").toInt() != 0;
  if (server.hasArg("mode"))  ac.mode  = (uint8_t)server.arg("mode").toInt();
  if (server.hasArg("temp"))  ac.temp  = constrain((int)server.arg("temp").toInt(), 16, 30);
  if (server.hasArg("fan")) {
    int i = constrain((int)server.arg("fan").toInt(), 0, 5);
    ac.fan = (i == 0) ? kBosch144FanAuto : kFanSteps[i - 1];
  }
  // 温度/モード/風量を変えたら電源も入れる
  if (server.hasArg("mode") || server.hasArg("temp") || server.hasArg("fan")) ac.power = true;

  if (kIrTxEnabled) {
    AcIr::send(ac);
    Serial.println("Web -> IR 送信");
  } else {
    Serial.println("Web 操作 (IR送信は無効化中)");
  }

  server.sendHeader("Location", "/");  // PRG パターン
  server.send(303, "text/plain", "");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  AcIr::begin(kIrTxGpio, kIrRxPin);
  AcIr::setTxEnabled(kIrTxEnabled);
  Serial.printf("IR送信: %s\n", kIrTxEnabled ? "有効" : "無効");

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(kHostname);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi 接続中");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.printf("\n接続完了 IP: %s\n", WiFi.localIP().toString().c_str());

  if (MDNS.begin(kHostname)) {
    Serial.printf("http://%s.local/ でアクセス可能\n", kHostname);
    MDNS.addService("http", "tcp", 80);
  }

  server.on("/", handleRoot);
  server.on("/state", handleState);
  server.on("/set", handleSet);
  server.onNotFound([]() { server.send(404, "text/plain", "Not Found"); });
  server.begin();
  Serial.println("Web サーバ開始");
}

void loop() {
  static uint32_t lastRxMs = 0;
  static uint32_t lastNoRxLogMs = 0;

  server.handleClient();

  // 物理リモコン (または自分の送信) を受信して状態を同期
  AcIr::RxDebugInfo dbg;
  AcIr::PollResult pr = AcIr::poll(ac, &dbg);
  if (pr == AcIr::PollResult::Bosch144) {
    lastRxMs = millis();
    Serial.printf("IR受信(BOSCH144) -> 同期: power=%d mode=%d temp=%d fan=%u bits=%u\n",
                  ac.power, ac.mode, ac.temp, ac.fan, dbg.bits);
  } else if (pr == AcIr::PollResult::NonBosch144) {
    lastRxMs = millis();
    Serial.printf("IR受信(非BOSCH144): type=%d bits=%u overflow=%d\n",
                  (int)dbg.type, dbg.bits, dbg.overflow);
  }

  uint32_t now = millis();
  if (now - lastRxMs >= 5000 && now - lastNoRxLogMs >= 5000) {
    Serial.println("IR受信なし(直近5秒)");
    lastNoRxLogMs = now;
  }
}
