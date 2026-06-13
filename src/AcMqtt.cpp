#include "AcMqtt.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

namespace {
WiFiClient   net;
PubSubClient mqtt(net);
AcState*     g_ac = nullptr;
void (*g_onCmd)() = nullptr;
uint32_t     lastTry = 0;

const char* kNodeId    = "toshiba_ac";
const char* kCmdTopic  = "toshiba_ac/ac/set";
const char* kStTopic   = "toshiba_ac/ac/state";
const char* kAvailTopic = "toshiba_ac/availability";
const char* kDiscoTopic = "homeassistant/climate/toshiba_ac/config";

// HA discovery (retained)。state/cmd は同一 topic に JSON、属性ごとに value/cmd template。
const char kDiscovery[] PROGMEM = R"json({
"name":"Toshiba AC","uniq_id":"toshiba_ac_climate",
"avty_t":"toshiba_ac/availability","pl_avail":"online","pl_not_avail":"offline",
"modes":["off","cool","dry","fan_only","heat","auto"],
"mode_stat_t":"toshiba_ac/ac/state","mode_stat_tpl":"{{ value_json.mode }}",
"mode_cmd_t":"toshiba_ac/ac/set","mode_cmd_tpl":"{\"mode\":\"{{ value }}\"}",
"min_temp":16,"max_temp":30,"temp_step":0.5,
"temp_stat_t":"toshiba_ac/ac/state","temp_stat_tpl":"{{ value_json.temp }}",
"temp_cmd_t":"toshiba_ac/ac/set","temp_cmd_tpl":"{\"temp\":{{ value }}}",
"fan_modes":["auto","20","40","60","80","100"],
"fan_mode_stat_t":"toshiba_ac/ac/state","fan_mode_stat_tpl":"{{ value_json.fan }}",
"fan_mode_cmd_t":"toshiba_ac/ac/set","fan_mode_cmd_tpl":"{\"fan\":\"{{ value }}\"}",
"dev":{"ids":["toshiba_ac"],"name":"Toshiba AC (Bosch144)","mf":"DIY","mdl":"ESP32-C3 IR Remote","sw":"1.0"}
})json";

const uint16_t kFanSteps[] = {kBosch144Fan20, kBosch144Fan40, kBosch144Fan60,
                              kBosch144Fan80, kBosch144Fan100};

const char* modeStr(const AcState& s) {
  if (!s.power) return "off";
  switch (s.mode) {
    case kBosch144Cool: return "cool";
    case kBosch144Dry:  return "dry";
    case kBosch144Fan:  return "fan_only";
    case kBosch144Heat: return "heat";
    case kBosch144Auto: return "auto";
    default:            return "cool";
  }
}

const char* fanStr(const AcState& s) {
  if (s.fan == kBosch144FanAuto || s.fan == kBosch144FanAuto0) return "auto";
  static const char* names[] = {"20", "40", "60", "80", "100"};
  for (uint8_t i = 0; i < 5; i++)
    if (s.fan == kFanSteps[i]) return names[i];
  return "auto";
}

void buildStateJson(char* buf, size_t n) {
  float t = g_ac->temp + (g_ac->half ? 0.5f : 0.0f);
  snprintf(buf, n, "{\"mode\":\"%s\",\"temp\":%.1f,\"fan\":\"%s\"}",
           modeStr(*g_ac), t, fanStr(*g_ac));
}

// HA からの属性JSON (例 {"mode":"cool"} / {"temp":26.5} / {"fan":"40"}) を部分適用
void applyCommandJson(const byte* payload, unsigned int len) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) return;

  if (!doc["mode"].isNull()) {
    const char* m = doc["mode"];
    if (strcmp(m, "off") == 0) {
      g_ac->power = false;
    } else {
      g_ac->power = true;
      if (!strcmp(m, "cool")) g_ac->mode = kBosch144Cool;
      else if (!strcmp(m, "dry")) g_ac->mode = kBosch144Dry;
      else if (!strcmp(m, "fan_only")) g_ac->mode = kBosch144Fan;
      else if (!strcmp(m, "heat")) g_ac->mode = kBosch144Heat;
      else if (!strcmp(m, "auto")) g_ac->mode = kBosch144Auto;
    }
  }
  if (!doc["temp"].isNull()) {
    int half2 = (int)(doc["temp"].as<float>() * 2 + 0.5f);
    if (half2 < 32) half2 = 32; else if (half2 > 60) half2 = 60;  // 16.0〜30.0
    g_ac->temp = half2 / 2;
    g_ac->half = half2 & 1;
    g_ac->power = true;
  }
  if (!doc["fan"].isNull()) {
    const char* f = doc["fan"];
    if (!strcmp(f, "auto")) g_ac->fan = kBosch144FanAuto;
    else {
      for (uint8_t i = 0; i < 5; i++) {
        char s[4]; snprintf(s, sizeof(s), "%d", (i + 1) * 20);
        if (!strcmp(f, s)) { g_ac->fan = kFanSteps[i]; break; }
      }
    }
    g_ac->power = true;
  }
  if (g_onCmd) g_onCmd();
}

void onMessage(char* topic, byte* payload, unsigned int len) {
  if (strcmp(topic, kCmdTopic) == 0) applyCommandJson(payload, len);
}

void publishDiscovery() {
  char buf[1024];
  strncpy_P(buf, kDiscovery, sizeof(buf));
  buf[sizeof(buf) - 1] = '\0';
  mqtt.publish(kDiscoTopic, buf, true);
}
}  // namespace

namespace AcMqtt {

void begin(AcState* state, void (*onCommand)()) {
  g_ac = state;
  g_onCmd = onCommand;
  net.setTimeout(2);  // TCP接続タイムアウト2秒 (既定30秒。未到達時に loop を長くブロックさせない)
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(1024);     // discovery JSON 用
  mqtt.setCallback(onMessage);
}

void publishState() {
  if (!mqtt.connected() || !g_ac) return;
  char buf[128];
  buildStateJson(buf, sizeof(buf));
  mqtt.publish(kStTopic, buf, true);  // retained
}

void loop() {
  if (mqtt.connected()) { mqtt.loop(); return; }
  if (WiFi.status() != WL_CONNECTED) return;
  uint32_t now = millis();
  if (now - lastTry < 15000) return;  // 失敗時は15秒間隔 (未接続時のWeb stall を最小化)
  lastTry = now;
  if (mqtt.connect(kNodeId, MQTT_USER, MQTT_PASS, kAvailTopic, 0, true, "offline")) {
    mqtt.publish(kAvailTopic, "online", true);
    mqtt.subscribe(kCmdTopic);
    publishDiscovery();
    publishState();
    Serial.println("MQTT 接続");
  }
}

bool connected() { return mqtt.connected(); }

}  // namespace AcMqtt
