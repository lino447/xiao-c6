# XIAO ESP32-C6 藍牙（BLE）網頁遙控 起手專案

用瀏覽器（Web Bluetooth）直接連線 Seeed Studio XIAO ESP32-C6，當成「藍牙車鑰匙」：透過繼電器控制 **解鎖／上鎖／開前箱／開後箱**，也保留板載 LED、GPIO 與 PWM 控制，不需要安裝 App。iPhone 可用 **Bluefy** 瀏覽器開啟。

線上頁面已部署於 https://lino447.github.io/xiao-c6/ ，可在 iPhone 上透過 Bluefy 瀏覽器使用。

在 iPhone 上請用 Bluefy 瀏覽器開啟。

```
xiao-c6-ble/
├── firmware/xiao_c6_ble/xiao_c6_ble.ino   ← 燒進 XIAO ESP32-C6 的 Arduino 程式
├── web/index.html                         ← 操控網頁（單一檔案，免建置）
├── web/car_crop.jpg, web/board.jpg        ← 網頁用圖片（授權見 web/CREDITS.txt）
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
  **iPhone / iPad**：Safari 與 iOS 版 Chrome 不支援，請從 App Store 安裝免費的 **Bluefy** 瀏覽器開啟。Firefox 不支援。
- **網址**：必須是 **HTTPS** 或 **localhost**。直接雙擊開啟 `file://` 通常無法使用。

### 方法 A：本機 localhost（電腦測試最快）
```bash
cd web
python3 -m http.server 8000
```
用 Chrome 開啟 <http://localhost:8000>，按「搜尋並連線」→ 選擇 **XIAO-C6**。

### 方法 B：GitHub Pages（手機也能用）
1. 建立一個 GitHub repo，把 `web/` 裡的 `index.html`、`car_crop.jpg`、`board.jpg`、`CREDITS.txt` 上傳到根目錄。
2. repo → **Settings → Pages** → Source 選 `main` 分支、`/ (root)`，儲存。
3. 約一分鐘後用 Android Chrome 或 iPhone 的 Bluefy 開啟 `https://<你的帳號>.github.io/<repo>/`。

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
| `UNLOCK` | 解鎖：D0 送出脈衝，回覆 `OK LOCK=0` |
| `LOCK` | 上鎖：D1 送出脈衝，回覆 `OK LOCK=1` |
| `FRUNK` | 開前箱：D2 送出脈衝，回覆 `OK FRUNK=1` |
| `TRUNK` | 開後箱：D3 送出脈衝，回覆 `OK TRUNK=1` |
| `CLOSE_FRUNK` / `CLOSE_TRUNK` | 只把前箱／後箱「狀態」改回已關閉（不送脈衝），回覆 `OK FRUNK=0` / `OK TRUNK=0` |
| `STATUS` | 回報 `STATUS LED=<0\|1> UPTIME=<秒>s LOCK=<0\|1\|?> FRUNK=<0\|1> TRUNK=<0\|1>` |
| `HELP` | 列出指令 |

回覆（Notify）：`OK ...`、`ERR ...` 或 `STATUS ...`。

### 車輛遙控接線（繼電器）

預設腳位（在 `.ino` 開頭的 `PIN_UNLOCK`、`PIN_LOCK`、`PIN_FRUNK`、`PIN_TRUNK` 修改）：

| 功能 | 指令 | 板上標示 | GPIO |
|---|---|---|---|
| 解鎖 | `UNLOCK` | D0 | 0 |
| 上鎖 | `LOCK` | D1 | 1 |
| 開前箱 | `FRUNK` | D2 | 2 |
| 開後箱 | `TRUNK` | D3 | 21 |

接法（以常見的 4 路繼電器模組為例）：
- XIAO `3V3`（或 `5V`，依模組規格）→ 繼電器模組 `VCC`；XIAO `GND` → 模組 `GND`
- XIAO `D0`～`D3` → 模組 `IN1`～`IN4`
- 繼電器的 `COM` / `NO` 接點並聯在原廠遙控器（或車上對應開關）的按鍵兩端，等於「代替手指按一下」。

行為說明：
- 每個指令讓對應腳位輸出 **`RELAY_PULSE_MS` = 500 ms** 的觸發電位後恢復，不會卡住。脈衝進行中再送車輛指令會回 `ERR 忙碌中`。
- 觸發電位 **`RELAY_ACTIVE_LEVEL` 預設為 HIGH**；低電位觸發的模組改成 `LOW`。開機時四個腳位都先設為不觸發。
- 每次車輛指令，板載 LED 會閃一下（`BLINK_MS` = 120 ms，LED 原本亮著就短暫熄滅），之後恢復原本狀態（含 PWM 亮度）。
- **車門狀態**：開機後為 `LOCK=?`（未知），送過 `LOCK`／`UNLOCK` 後才會記錄。這是韌體記住的「最後一次指令」，不是車上的實際感測值。
- **前箱／後箱**：沒有感測器，所以送出 `FRUNK`／`TRUNK` 後標記為已開啟，**`LID_AUTO_CLOSE_SEC` = 30 秒後自動改回已關閉**並主動通知網頁；也可用 `CLOSE_FRUNK`／`CLOSE_TRUNK` 手動改回。把 `LID_AUTO_CLOSE_SEC` 設成 `0` 就會一直維持已開啟直到手動關閉。
- D0～D3 仍在 `ALLOWED_PINS` 內，`PIN`／`PWM` 指令也能改動它們；接了繼電器後請避免這樣做。

> ⚠️ 安全提醒：改裝車輛電路前請確認不影響原廠防盜與保固；建議接在備用遙控器的按鍵上，不要直接接車身線路。BLE 連線目前沒有配對碼保護，附近的人只要有網頁也能連線，實際上車前請自行加上驗證。

### 網頁介面
- **車輛**：上鎖／解鎖／開前箱／開後箱按鈕，車門、前箱、後箱狀態，連線狀態。Web Bluetooth 通常讀不到連線中的訊號強度（RSSI），所以只顯示「已連線／未連線」，瀏覽器有回報時才顯示 dBm。
- **裝置**：「搜尋並連線」會開啟瀏覽器內建的裝置選單（網頁本身無法列出附近裝置）。
- **紀錄**：所有送出與收到的訊息。
- **帳戶**：車輛詳情、接線對照、「靠近車輛時自動連線」開關，以及 **進階／開發者** 區（LED、PWM、自訂指令）。
  自動連線需要瀏覽器支援 `getDevices()` 與 `watchAdvertisements()`；不支援時只會在意外斷線後自動重試 3 次。

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
| 按「搜尋並連線」沒反應／顯示不支援 | 確認用 Chrome / Edge（iPhone 用 Bluefy），且網址是 `https://` 或 `http://localhost`。 |
| 選單中找不到 XIAO-C6 | 確認韌體已上傳、序列埠顯示「BLE 廣播中」；電腦／手機藍牙已開啟；板子不要離太遠。若有其他裝置（手機 App、nRF Connect）正連著板子，先中斷。 |
| Android 掃不到 | 開啟手機 **定位服務**，並允許 Chrome 的「附近的裝置」權限。 |
| Linux 掃不到 | 到 `chrome://flags` 啟用 **Experimental Web Platform features**（`#enable-experimental-web-platform-features`），並確認 BlueZ 正常運作。 |
| 改了 UUID 後連不上／找不到服務 | 系統可能快取舊的 GATT 資料：在系統藍牙設定中「忘記此裝置」，或重開藍牙。 |
| 連線後馬上斷線 | 換一條 USB 線／電源；按 RESET 重啟板子後再連。 |
| 斷線後無法重連 | 韌體會在斷線後自動重新廣播；網頁也會自動重試 3 次，失敗再按「搜尋並連線」。 |
| 按上鎖／解鎖沒反應 | 看序列埠是否有 `[指令] LOCK`；用三用電表量 D0～D3 是否有 500 ms 脈衝；確認繼電器模組是高／低電位觸發並調整 `RELAY_ACTIVE_LEVEL`。 |
| 天線訊號弱 | XIAO ESP32-C6 預設用板載陶瓷天線；若接外接天線，需透過 GPIO3/GPIO14 切換（請參考 Seeed Wiki）。 |
| 上傳時找不到連接埠 | 按住 BOOT 再接 USB 進入下載模式；Windows 確認驅動程式。 |
| 序列埠沒輸出 | Arduino IDE「工具 → USB CDC On Boot」需為 **Enabled**（XIAO_ESP32C6 預設已啟用）。 |
