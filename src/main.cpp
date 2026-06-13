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
const uint8_t kIrTxGpio = 4;  // IR LED
const uint8_t kIrRxPin = 5;   // 受信モジュール OUT

WebServer server(80);
AcState ac;  // 現在の状態 (Web と受信で共有)

// UI の風量 1..5 と Bosch144 ファンコードの対応
const uint16_t kFanSteps[] = {kBosch144Fan20, kBosch144Fan40, kBosch144Fan60,
                              kBosch144Fan80, kBosch144Fan100};

// 風量コード -> UI インデックス(0:自動, 1..5)
uint8_t fanIndex() {
  if (ac.fan == kBosch144FanAuto || ac.fan == kBosch144FanAuto0) return 0;
  for (uint8_t i = 0; i < 5; i++)
    if (ac.fan == kFanSteps[i]) return i + 1;
  return 0;
}

// 現在状態を JSON で返す (fan は UI インデックス, temp は 0.5 刻みの数値)
String stateJson() {
  String temp = String(ac.temp) + (ac.half ? ".5" : "");
  return String("{\"power\":") + (ac.power ? 1 : 0) +
         ",\"mode\":" + ac.mode + ",\"temp\":" + temp +
         ",\"fan\":" + fanIndex() + "}";
}

// ---- HTML (初回のみ配信。以降は /set, /state の JSON で部分更新) ----
const char kHtml[] PROGMEM = R"HTML(<!DOCTYPE html><html lang=ja><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>東芝エアコン リモコン</title><style>
body{font-family:sans-serif;max-width:420px;margin:0 auto;padding:16px;background:#0f172a;color:#e2e8f0}
h1{font-size:1.3rem}.card{background:#1e293b;border-radius:12px;padding:16px;margin-bottom:14px}
.row{display:flex;align-items:center;justify-content:space-between;margin:10px 0}
.btn{background:#334155;color:#e2e8f0;border:none;border-radius:8px;padding:10px 14px;font-size:1rem;cursor:pointer;margin:2px}
.btn.on{background:#2563eb}.big{font-size:2rem;font-weight:bold}.pw{background:#16a34a}.po{background:#dc2626}
</style></head><body>
<h1>東芝エアコン リモコン</h1>
<div class=card><div class=row><span>電源</span><span class=big id=pw>-</span></div>
<div class=row><button class="btn pw" onclick="set('power',1)">ON</button>
<button class="btn po" onclick="set('power',0)">OFF</button></div></div>
<div class=card><div class=row><span>運転モード</span></div><div id=modes></div></div>
<div class=card><div class=row><span>温度</span><span class=big id=temp>-</span></div>
<div class=row><button class=btn onclick="set('temp',st.temp-0.5)">&minus;</button>
<button class=btn onclick="set('temp',st.temp+0.5)">&plus;</button></div></div>
<div class=card><div class=row><span>風量</span></div><div id=fans></div></div>
<script>
const M=[[5,'自動'],[0,'冷房'],[3,'除湿'],[6,'暖房'],[2,'送風']];
const FN=['自動','20%','40%','60%','80%','100%'];
let st={power:0,mode:0,temp:26,fan:0};
function render(){
pw.textContent=st.power?'ON':'OFF';temp.textContent=st.temp+'℃';
modes.innerHTML=M.map(([c,n])=>`<button class="btn ${st.mode==c?'on':''}" onclick="set('mode',${c})">${n}</button>`).join('');
fans.innerHTML=FN.map((n,i)=>`<button class="btn ${st.fan==i?'on':''}" onclick="set('fan',${i})">${n}</button>`).join('');}
async function set(k,v){st=await(await fetch(`/set?${k}=${v}`)).json();render();}
async function poll(){try{let s=await(await fetch('/state')).json();
if(JSON.stringify(s)!=JSON.stringify(st)){st=s;render();}}catch(e){}}
render();poll();setInterval(poll,1000);
</script></body></html>)HTML";

void handleRoot() { server.send_P(200, "text/html; charset=utf-8", kHtml); }
void handleState() { server.send(200, "application/json", stateJson()); }

void handleSet() {
  if (server.hasArg("power")) ac.power = server.arg("power").toInt() != 0;
  if (server.hasArg("mode"))  ac.mode  = (uint8_t)server.arg("mode").toInt();
  if (server.hasArg("temp")) {
    int half2 = constrain((int)(server.arg("temp").toFloat() * 2 + 0.5f), 32, 60);  // 0.5 刻み (16.0〜30.0)
    ac.temp = half2 / 2;
    ac.half = half2 & 1;
  }
  if (server.hasArg("fan")) {
    int i = constrain((int)server.arg("fan").toInt(), 0, 5);
    ac.fan = (i == 0) ? kBosch144FanAuto : kFanSteps[i - 1];
  }
  // 温度/モード/風量を変えたら電源も入れる
  if (server.hasArg("mode") || server.hasArg("temp") || server.hasArg("fan")) ac.power = true;

  AcIr::send(ac);
  Serial.println("Web -> IR 送信");
  server.send(200, "application/json", stateJson());  // 全ページ再読込せず JSON だけ返す
}

void setup() {
  Serial.begin(115200);
  delay(200);

  AcIr::begin(kIrTxGpio, kIrRxPin);

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
  server.handleClient();

  // 物理リモコン (または自分の送信) を受信して状態を同期
  if (AcIr::poll(ac))
    Serial.printf("IR受信 -> 同期: power=%d mode=%d temp=%d%s fan=%u\n",
                  ac.power, ac.mode, ac.temp, ac.half ? ".5" : "", ac.fan);
}
