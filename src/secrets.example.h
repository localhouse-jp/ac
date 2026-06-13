// テンプレート: src/secrets.h にコピーして自分の WiFi 情報を記入する
//   cp src/secrets.example.h src/secrets.h
#pragma once
#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASS "your-wifi-password"

// --- MQTT (Home Assistant の Mosquitto ブローカー) ---
#define MQTT_HOST "192.168.1.10"   // HA / ブローカーの IP
#define MQTT_PORT 1883             // 数値
#define MQTT_USER "mqttuser"
#define MQTT_PASS "mqttpass"
