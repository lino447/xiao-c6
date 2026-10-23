# XIAO ESP32-C6 藍牙（BLE）網頁遙控 起手專案

用瀏覽器（Web Bluetooth）直接連線 Seeed Studio XIAO ESP32-C6，控制板載 LED、GPIO 與 PWM，不需要安裝 App。

線上頁面已部署於 https://lino447.github.io/xiao-c6/ ，可在 iPhone 上透過 Bluefy 瀏覽器使用。

```
xiao-c6-ble/
├── firmware/xiao_c6_ble/xiao_c6_ble.ino   ← 燒進 XIAO ESP32-C6 的 Arduino 程式
├── web/index.html                         ← 操控網頁（單一檔案，免建置）
└── README.md
```

---

## 1. Arduino IDE 設定

1. 安裝 [Arduino IDE 2.x](https://www.arduino.cc/en/software)。
2. **檔案 → 偏好設定 → 額外的開發板管理員網址**，貼上：
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. **工具 → 開發板 → 開發板管理員**，搜尋 `esp32`，安裝 **esp32 by Espressif Systems**（需 **3.0 以上**；本專案以 3.3.12 編譯驗證通過）。
4. **工具 → 開發板 → esp32 → XIAO_ESP32C6**。
5. 用 USB-C 線接上板子，**工具 → 連接埠** 選擇對應的序列埠。

### 函式庫
**不需要另外安裝。** 程式使用 esp32 core 內建的 `BLE` 函式庫（`#include <BLEDevice.h>`）。
（若你另外裝了 NimBLE-Arduino 也不影響，本程式不使用它。）

## 2. 上傳韌體

1. 開啟 `firmware/xiao_c6_ble/xiao_c6_ble.ino`。
2. 按「上傳」。
3. 開啟 **序列埠監控視窗**（115200 baud），應看到：
   ```
   === XIAO ESP32-C6 BLE 遙控 啟動 ===
   BLE 廣播中，裝置名稱: XIAO-C6
   ```

> 若上傳失敗：按住板子上的 **BOOT** 鍵，插上 USB（或按一下 RESET），放開 BOOT 後再上傳。

使用 arduino-cli 的話：
```bash
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C6 firmware/xiao_c6_ble
arduino-cli upload  --fqbn esp32:esp32:XIAO_ESP32C6 -p /dev/ttyACM0 firmware/xiao_c6_ble
```

## 3. 開啟操控網頁

Web Bluetooth 有兩個限制：
- **瀏覽器**：電腦版（Windows / macOS / Linux / ChromeOS）或 **Android** 的 **Chrome / Edge**。
  **iPhone / iPad 不支援**（Safari 及 iOS 上所有瀏覽器都不行），Firefox 也不支援。
- **網址**：必須是 **HTTPS** 或 **localhost**。直接雙擊開啟 `file://` 通常無法使用。

### 方法 A：本機 localhost（電腦測試最快）
```bash
cd web
python3 -m http.server 8000
```
用 Chrome 開啟 <http://localhost:8000>，按「連線裝置」→ 選擇 **XIAO-C6**。

### 方法 B：GitHub Pages（手機也能用）
1. 建立一個 GitHub repo，把 `web/index.html` 上傳到根目錄。
2. repo → **Settings → Pages** → Source 選 `main` 分支、`/ (root)`，儲存。
3. 約一分鐘後用 Android Chrome 開啟 `https://<你的帳號>.github.io/<repo>/`。

（Netlify、Vercel、Cloudflare Pages 等任何 HTTPS 靜態網站也都可以。）

---

## 4. BLE 規格與指令

| 項目 | 值 |
|---|---|
| 裝置名稱 | `XIAO-C6` |
| Service UUID | `903cabbe-a880-4bb1-894b-714475b081bc` |
| 指令特徵值（Write / Write Without Response） | `c86aa8bf-03dc-4a01-acdd-f6e3f79ddcd4` |
| 狀態特徵值（Read / Notify） | `ca7e628e-65c3-4440-a4e8-2fcfd2228ac3` |

指令為 UTF-8 文字，不分大小寫，逗號或空白分隔皆可：

| 指令 | 說明 |
|---|---|
| `LED,ON` / `LED,OFF` / `LED,TOGGLE` | 板載 LED 開／關／切換（也可只送 `ON`、`OFF`、`TOGGLE`） |
| `PIN,<腳位>,<0\|1>` | 指定 GPIO 輸出低／高電位，例如 `PIN,2,1` |
| `PWM,<腳位>,<0-255>` | PWM 輸出（5 kHz、8 bit），例如 `PWM,0,128`；腳位 15（LED）已反相，255 = 最亮 |
| `STATUS` | 回報 `STATUS LED=<0\|1> UPTIME=<秒>s` |
| `HELP` | 列出指令 |

回覆（Notify）：`OK ...`、`ERR ...` 或 `STATUS ...`。

### 可控制的腳位（GPIO 編號）
| 板上標示 | D0 | D1 | D2 | D3 | D4 | D5 | D6 | D7 | D8 | D9 | D10 | 使用者 LED |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| GPIO | 0 | 1 | 2 | 21 | 22 | 23 | 16 | 17 | 19 | 20 | 18 | 15 |

板載使用者 LED 在 **GPIO15**，**低電位點亮**（active-low，`LED_BUILTIN = 15`）。
為了安全，韌體刻意不允許控制 GPIO12/13（USB）、GPIO3/14（天線切換）、GPIO9（BOOT）等。要開放其他腳位，修改 `.ino` 裡的 `ALLOWED_PINS`。
注意：D6/D7（GPIO16/17）也是 UART TX/RX。

### 擴充
在 `handleCommand()` 內加一個 `else if (op == "你的指令")` 分支，網頁端用自訂指令框或新增按鈕 `data-cmd="你的指令"` 即可。
若修改 UUID，記得 `.ino` 與 `index.html` 兩邊都要改。

---

## 5. 疑難排解

| 狀況 | 解法 |
|---|---|
| 按「連線裝置」沒反應／顯示不支援 | 確認用 Chrome / Edge，且網址是 `https://` 或 `http://localhost`。iOS 無法使用。 |
| 選單中找不到 XIAO-C6 | 確認韌體已上傳、序列埠顯示「BLE 廣播中」；電腦／手機藍牙已開啟；板子不要離太遠。若有其他裝置（手機 App、nRF Connect）正連著板子，先中斷。 |
| Android 掃不到 | 開啟手機 **定位服務**，並允許 Chrome 的「附近的裝置」權限。 |
| Linux 掃不到 | 到 `chrome://flags` 啟用 **Experimental Web Platform features**（`#enable-experimental-web-platform-features`），並確認 BlueZ 正常運作。 |
| 改了 UUID 後連不上／找不到服務 | 系統可能快取舊的 GATT 資料：在系統藍牙設定中「忘記此裝置」，或重開藍牙。 |
| 連線後馬上斷線 | 換一條 USB 線／電源；按 RESET 重啟板子後再連。 |
| 斷線後無法重連 | 韌體會在斷線後自動重新廣播；網頁也會自動重試 3 次，失敗再按「連線裝置」。 |
| 天線訊號弱 | XIAO ESP32-C6 預設用板載陶瓷天線；若接外接天線，需透過 GPIO3/GPIO14 切換（請參考 Seeed Wiki）。 |
| 上傳時找不到連接埠 | 按住 BOOT 再接 USB 進入下載模式；Windows 確認驅動程式。 |
| 序列埠沒輸出 | Arduino IDE「工具 → USB CDC On Boot」需為 **Enabled**（XIAO_ESP32C6 預設已啟用）。 |
