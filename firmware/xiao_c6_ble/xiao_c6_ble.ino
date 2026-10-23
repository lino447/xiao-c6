/*
 * XIAO ESP32-C6 BLE 網頁遙控 — 韌體
 * ---------------------------------------------------------------
 * 開發環境：Arduino IDE / arduino-cli
 *   板子套件：esp32 by Espressif Systems（arduino-esp32 core 3.x，已於 3.3.12 編譯驗證）
 *   開發板  ：XIAO_ESP32C6（FQBN: esp32:esp32:XIAO_ESP32C6）
 *   BLE 函式庫：使用 core 內建的「BLE」函式庫（BLEDevice.h），不需另外安裝。
 *
 * 功能：
 *   - BLE 裝置名稱 "XIAO-C6"，提供自訂 Service
 *   - 指令特徵值（Write）：接收網頁送來的文字指令
 *   - 狀態特徵值（Read + Notify）：回傳執行結果與狀態
 *   - 控制板載使用者 LED（GPIO15，低電位點亮 active-low）
 *   - 斷線後自動重新廣播，可隨時重新連線
 *   - 車輛遙控：LOCK / UNLOCK / FRUNK / TRUNK 會在對應腳位送出短暫脈衝
 *     （接繼電器模組，模擬按下原廠遙控器按鍵），每次動作板載 LED 閃一下
 *
 * 指令格式（UTF-8 文字，不分大小寫，可用逗號或空白分隔）：
 *   LED,ON | LED,OFF | LED,TOGGLE   （也可直接送 ON / OFF / TOGGLE）
 *   PIN,<腳位>,<0|1>                 設定 GPIO 輸出高/低電位
 *   PWM,<腳位>,<0-255>               PWM 輸出（對 LED 腳位 15 時 255=最亮）
 *   UNLOCK / LOCK                     解鎖 / 上鎖（脈衝）
 *   FRUNK / TRUNK                     開前箱 / 開後箱（脈衝）
 *   CLOSE_FRUNK / CLOSE_TRUNK         手動把前箱 / 後箱狀態標記為關閉（不送脈衝）
 *   STATUS                            回報目前狀態
 *   HELP                              列出指令
 * 回覆格式：
 *   OK <說明>  /  ERR <原因>
 *   STATUS LED=<0|1> UPTIME=<秒>s LOCK=<0|1|?> FRUNK=<0|1> TRUNK=<0|1>
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ===== 可自行修改的設定 =====
#define DEVICE_NAME "XIAO-C6"

// 自訂 UUID（網頁端 index.html 必須使用相同的值）
#define SERVICE_UUID     "903cabbe-a880-4bb1-894b-714475b081bc"
#define CMD_CHAR_UUID    "c86aa8bf-03dc-4a01-acdd-f6e3f79ddcd4"  // 網頁 → 板子（Write）
#define STATUS_CHAR_UUID "ca7e628e-65c3-4440-a4e8-2fcfd2228ac3"  // 板子 → 網頁（Read/Notify）

// 板載使用者 LED：GPIO15，低電位點亮
#define USER_LED_PIN 15
#define LED_ON_LEVEL  LOW
#define LED_OFF_LEVEL HIGH

#define PWM_FREQ_HZ  5000
#define PWM_RES_BITS 8

// ===== 車輛遙控（繼電器）設定 =====
// 預設：D0=解鎖、D1=上鎖、D2=開前箱、D3=開後箱（GPIO 編號）
#define PIN_UNLOCK 0    // D0
#define PIN_LOCK   1    // D1
#define PIN_FRUNK  2    // D2
#define PIN_TRUNK  21   // D3
// 繼電器觸發電位：多數 3.3V/5V 繼電器模組為高電位觸發（HIGH）；低電位觸發模組改成 LOW
#define RELAY_ACTIVE_LEVEL HIGH
#define RELAY_IDLE_LEVEL   (RELAY_ACTIVE_LEVEL == HIGH ? LOW : HIGH)
// 脈衝長度（毫秒）：模擬按住遙控器按鍵的時間
#define RELAY_PULSE_MS 500
// 前箱 / 後箱開啟後，幾秒後自動視為「已關閉」（沒有感測器，只能用時間推估）
// 設為 0 = 一直維持「已開啟」，直到收到 CLOSE_FRUNK / CLOSE_TRUNK
#define LID_AUTO_CLOSE_SEC 30
// 每次車輛指令板載 LED 閃爍的時間（毫秒）
#define BLINK_MS 120

// 允許控制的腳位：XIAO 外露的 D0~D10 + 板載 LED
// D0=GPIO0 D1=GPIO1 D2=GPIO2 D3=GPIO21 D4=GPIO22 D5=GPIO23
// D6=GPIO16 D7=GPIO17 D8=GPIO19 D9=GPIO20 D10=GPIO18
// （刻意排除 GPIO12/13 USB、GPIO3/14 天線切換、GPIO9 BOOT 等）
const uint8_t ALLOWED_PINS[] = {0, 1, 2, 21, 22, 23, 16, 17, 19, 20, 18, USER_LED_PIN};

// ===== 全域狀態 =====
BLEServer *pServer = nullptr;
BLECharacteristic *pStatusChar = nullptr;
volatile bool deviceConnected = false;
bool oldDeviceConnected = false;

bool ledOn = false;
int ledPwmDuty = -1;           // 板載 LED 目前的 PWM 值（-1 = 數位模式）
uint64_t pwmAttachedMask = 0;  // 記錄哪些腳位目前是 PWM 模式

// 車輛狀態：lockState -1 = 未知（開機後尚未操作）、0 = 已解鎖、1 = 已上鎖
int lockState = -1;
bool frunkOpen = false, trunkOpen = false;
uint32_t frunkOpenedAt = 0, trunkOpenedAt = 0;

// 非阻塞脈衝：同一時間只允許一個繼電器脈衝
int pulsePin = -1;
uint32_t pulseStartedAt = 0;
// 非阻塞 LED 閃爍
bool blinking = false;
uint32_t blinkStartedAt = 0;

// 指令佇列：在 BLE 回呼中只存字串，於 loop() 中處理（避免在回呼內做太多事）
portMUX_TYPE cmdMux = portMUX_INITIALIZER_UNLOCKED;
String pendingCmd;
volatile bool hasPendingCmd = false;

// ===== 工具函式 =====
bool isAllowedPin(int pin) {
  for (uint8_t p : ALLOWED_PINS) {
    if (p == pin) return true;
  }
  return false;
}

bool isPwm(int pin) { return pwmAttachedMask & (1ULL << pin); }

// 若腳位正在 PWM 模式，切回一般數位輸出
void ensureDigital(int pin) {
  if (isPwm(pin)) {
    ledcDetach(pin);
    pwmAttachedMask &= ~(1ULL << pin);
  }
  pinMode(pin, OUTPUT);
}

void sendStatus(const String &msg) {
  Serial.print("[回覆] ");
  Serial.println(msg);
  if (pStatusChar) {
    pStatusChar->setValue(msg.c_str());
    if (deviceConnected) pStatusChar->notify();
  }
}

void setLed(bool on) {
  ensureDigital(USER_LED_PIN);
  ledOn = on;
  ledPwmDuty = -1;
  digitalWrite(USER_LED_PIN, on ? LED_ON_LEVEL : LED_OFF_LEVEL);
}

String statusString() {
  String s = "STATUS LED=";
  s += ledOn ? "1" : "0";
  s += " UPTIME=";
  s += String(millis() / 1000);
  s += "s LOCK=";
  s += lockState < 0 ? "?" : String(lockState);
  s += " FRUNK=";
  s += frunkOpen ? "1" : "0";
  s += " TRUNK=";
  s += trunkOpen ? "1" : "0";
  return s;
}

bool isCarPin(int pin) {
  return pin == PIN_UNLOCK || pin == PIN_LOCK || pin == PIN_FRUNK || pin == PIN_TRUNK;
}

// 依目前 LED 狀態（數位或 PWM）把板載 LED 實際輸出寫回去
void writeLedOutput(bool forceOn) {
  if (ledPwmDuty >= 0 && isPwm(USER_LED_PIN)) {
    ledcWrite(USER_LED_PIN, 255 - (forceOn ? 255 : ledPwmDuty));
  } else {
    digitalWrite(USER_LED_PIN, (forceOn || ledOn) ? LED_ON_LEVEL : LED_OFF_LEVEL);
  }
}

// 板載 LED 短暫閃爍（LED 原本亮著就短暫熄滅），結束後恢復原狀
void startBlink() {
  blinking = true;
  blinkStartedAt = millis();
  if (ledOn) {
    if (ledPwmDuty >= 0 && isPwm(USER_LED_PIN)) ledcWrite(USER_LED_PIN, 255);
    else digitalWrite(USER_LED_PIN, LED_OFF_LEVEL);
  } else {
    writeLedOutput(true);
  }
}

// 開始一個繼電器脈衝；若前一個脈衝還沒結束則回傳 false
bool startPulse(int pin) {
  if (pulsePin >= 0) return false;
  ensureDigital(pin);
  digitalWrite(pin, RELAY_ACTIVE_LEVEL);
  pulsePin = pin;
  pulseStartedAt = millis();
  startBlink();
  return true;
}

// 在 loop() 中呼叫：結束脈衝、LED 閃爍，以及前後箱自動關閉
void serviceTimers() {
  uint32_t now = millis();
  if (pulsePin >= 0 && now - pulseStartedAt >= RELAY_PULSE_MS) {
    digitalWrite(pulsePin, RELAY_IDLE_LEVEL);
    pulsePin = -1;
  }
  if (blinking && now - blinkStartedAt >= BLINK_MS) {
    blinking = false;
    writeLedOutput(false);
  }
#if LID_AUTO_CLOSE_SEC > 0
  bool changed = false;
  if (frunkOpen && now - frunkOpenedAt >= LID_AUTO_CLOSE_SEC * 1000UL) { frunkOpen = false; changed = true; }
  if (trunkOpen && now - trunkOpenedAt >= LID_AUTO_CLOSE_SEC * 1000UL) { trunkOpen = false; changed = true; }
  if (changed) sendStatus(statusString());
#endif
}

void carCommand(const String &op) {
  int pin;
  if (op == "UNLOCK") pin = PIN_UNLOCK;
  else if (op == "LOCK") pin = PIN_LOCK;
  else if (op == "FRUNK") pin = PIN_FRUNK;
  else pin = PIN_TRUNK;
  if (!startPulse(pin)) { sendStatus("ERR 忙碌中，請稍後再試"); return; }
  if (op == "UNLOCK") { lockState = 0; sendStatus("OK LOCK=0"); }
  else if (op == "LOCK") { lockState = 1; sendStatus("OK LOCK=1"); }
  else if (op == "FRUNK") { frunkOpen = true; frunkOpenedAt = millis(); sendStatus("OK FRUNK=1"); }
  else { trunkOpen = true; trunkOpenedAt = millis(); sendStatus("OK TRUNK=1"); }
}

// ===== 指令解析 =====
void handleCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  cmd.replace(' ', ',');
  Serial.print("[指令] ");
  Serial.println(cmd);
  if (cmd.length() == 0) return;

  // 拆成最多 3 段
  String part[3];
  int n = 0, start = 0;
  while (n < 3) {
    int idx = cmd.indexOf(',', start);
    String token = (idx < 0) ? cmd.substring(start) : cmd.substring(start, idx);
    token.trim();
    if (token.length() > 0) part[n++] = token;
    if (idx < 0) break;
    start = idx + 1;
  }

  String op = part[0];

  // 簡寫：ON / OFF / TOGGLE
  if (op == "ON" || op == "OFF" || op == "TOGGLE") {
    part[1] = op;
    op = "LED";
  }

  if (op == "LED") {
    if (part[1] == "ON" || part[1] == "1") setLed(true);
    else if (part[1] == "OFF" || part[1] == "0") setLed(false);
    else if (part[1] == "TOGGLE") setLed(!ledOn);
    else { sendStatus("ERR 用法: LED,ON|OFF|TOGGLE"); return; }
    sendStatus(String("OK LED=") + (ledOn ? "1" : "0"));
  }
  else if (op == "PIN") {
    if (n < 3) { sendStatus("ERR 用法: PIN,<腳位>,<0|1>"); return; }
    int pin = part[1].toInt();
    int val = part[2].toInt();
    if (!isAllowedPin(pin)) { sendStatus("ERR 不允許的腳位 " + part[1]); return; }
    if (val != 0 && val != 1) { sendStatus("ERR 值須為 0 或 1"); return; }
    if (isCarPin(pin) && pin == pulsePin) { sendStatus("ERR 腳位正在送出脈衝"); return; }
    ensureDigital(pin);
    digitalWrite(pin, val ? HIGH : LOW);
    if (pin == USER_LED_PIN) { ledOn = (val ? HIGH : LOW) == LED_ON_LEVEL; ledPwmDuty = -1; }
    sendStatus("OK PIN " + String(pin) + "=" + String(val));
  }
  else if (op == "PWM") {
    if (n < 3) { sendStatus("ERR 用法: PWM,<腳位>,<0-255>"); return; }
    int pin = part[1].toInt();
    int duty = part[2].toInt();
    if (!isAllowedPin(pin)) { sendStatus("ERR 不允許的腳位 " + part[1]); return; }
    if (duty < 0 || duty > 255) { sendStatus("ERR 值須為 0-255"); return; }
    if (!isPwm(pin)) {
      if (!ledcAttach(pin, PWM_FREQ_HZ, PWM_RES_BITS)) {
        sendStatus("ERR PWM 初始化失敗");
        return;
      }
      pwmAttachedMask |= (1ULL << pin);
    }
    // 板載 LED 為低電位點亮，所以反相，讓 255 = 最亮
    int out = (pin == USER_LED_PIN) ? (255 - duty) : duty;
    ledcWrite(pin, out);
    if (pin == USER_LED_PIN) { ledOn = duty > 0; ledPwmDuty = duty; }
    sendStatus("OK PWM " + String(pin) + "=" + String(duty));
  }
  else if (op == "LOCK" || op == "UNLOCK" || op == "FRUNK" || op == "TRUNK") {
    carCommand(op);
  }
  else if (op == "CLOSE_FRUNK") {
    frunkOpen = false;
    sendStatus("OK FRUNK=0");
  }
  else if (op == "CLOSE_TRUNK") {
    trunkOpen = false;
    sendStatus("OK TRUNK=0");
  }
  else if (op == "STATUS") {
    sendStatus(statusString());
  }
  else if (op == "HELP") {
    sendStatus("指令: LED,ON|OFF|TOGGLE / PIN,n,0|1 / PWM,n,0-255 / LOCK / UNLOCK / FRUNK / TRUNK / CLOSE_FRUNK / CLOSE_TRUNK / STATUS");
  }
  else {
    sendStatus("ERR 未知指令: " + op);
  }
}

// ===== BLE 回呼 =====
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    deviceConnected = true;
  }
  void onDisconnect(BLEServer *server) override {
    deviceConnected = false;
  }
};

class CmdCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *chr) override {
    String value = chr->getValue();  // core 3.x 回傳 Arduino String
    if (value.length() == 0) return;
    portENTER_CRITICAL(&cmdMux);
    pendingCmd = value;
    hasPendingCmd = true;
    portEXIT_CRITICAL(&cmdMux);
  }
};

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== XIAO ESP32-C6 BLE 遙控 啟動 ===");

  pinMode(USER_LED_PIN, OUTPUT);
  setLed(false);

  // 繼電器腳位先設為不觸發，避免開機時誤動作
  const int carPins[] = {PIN_UNLOCK, PIN_LOCK, PIN_FRUNK, PIN_TRUNK};
  for (int p : carPins) {
    digitalWrite(p, RELAY_IDLE_LEVEL);
    pinMode(p, OUTPUT);
    digitalWrite(p, RELAY_IDLE_LEVEL);
  }

  BLEDevice::init(DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *service = pServer->createService(SERVICE_UUID);

  BLECharacteristic *cmdChar = service->createCharacteristic(
      CMD_CHAR_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  cmdChar->setCallbacks(new CmdCallbacks());

  pStatusChar = service->createCharacteristic(
      STATUS_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pStatusChar->addDescriptor(new BLE2902());  // 讓網頁可以訂閱通知（CCCD）
  pStatusChar->setValue("READY");

  service->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("BLE 廣播中，裝置名稱: " DEVICE_NAME);
}

void loop() {
  // 處理收到的指令
  if (hasPendingCmd) {
    portENTER_CRITICAL(&cmdMux);
    String cmd = pendingCmd;
    hasPendingCmd = false;
    portEXIT_CRITICAL(&cmdMux);
    handleCommand(cmd);
  }

  serviceTimers();

  // 剛連線：送一次目前狀態
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = true;
    Serial.println("裝置已連線");
    delay(300);  // 等網頁訂閱通知
    sendStatus(statusString());
  }

  // 剛斷線：重新開始廣播，讓網頁可以再次連線
  if (!deviceConnected && oldDeviceConnected) {
    oldDeviceConnected = false;
    Serial.println("裝置已斷線，重新廣播中…");
    delay(500);  // 給 BLE 堆疊一點時間
    BLEDevice::startAdvertising();
  }

  delay(10);
}
