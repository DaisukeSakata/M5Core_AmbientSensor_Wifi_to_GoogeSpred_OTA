# M5Stack Core ESP32 アップロード手順ガイド (CLI版)

このドキュメントでは、VSCode + PlatformIO環境でM5Stack Core ESP32のコードを修正した後に、**コマンドライン（CLI）を使って確実にアップロードする手順**を説明します。

**重要**: VSCodeのGUIボタン（→アイコン）やショートカットキーは、環境切り替え時にキャッシュ問題が発生する可能性があるため、**コマンドラインからの実行を推奨します。**

---

## 📋 前提条件

- VSCodeがインストールされている
- PlatformIO拡張機能がインストールされている
- platformio.iniファイルが正しく設定されている
- M5Stack Core ESP32デバイスを使用

---

## 🚀 推奨アップロード方法（コマンドライン）

### ターミナルの開き方

VSCode内でターミナルを開きます：

- **ショートカット**: `` Ctrl + ` `` （バッククォート）
- **メニュー**: ターミナル → 新しいターミナル

---

## 🔌 USB接続でのアップロード（推奨手順）

### ステップ1: M5StackをUSBケーブルで接続

M5Stack Core ESP32とPCをUSB Type-Cケーブルで接続します。

### ステップ2: コマンドでアップロード

VSCodeのターミナル（PowerShell）で以下を実行：

```bash
pio run -e m5stack-core-esp32-16M -t upload
```

**コマンドの意味**:
- `pio run`: PlatformIOのビルド・実行コマンド
- `-e m5stack-core-esp32-16M`: USB環境を指定
- `-t upload`: アップロードタスクを実行

### ステップ3: アップロード完了の確認

ターミナルに以下のような表示が出ます:

```
Configuring upload protocol...
AVAILABLE: cmsis-dap, esp-bridge, esp-builtin, esp-prog, espota, esptool, iot-bus-jtag, jlink, minimodule, olimex-arm-usb-ocd, olimex-arm-usb-ocd-h, olimex-arm-usb-tiny-h, olimex-jtag-tiny, tumpa
CURRENT: upload_protocol = esptool
Looking for upload port...
Auto-detected: COM9
...
Writing at 0x00010000... (10 %)
Writing at 0x00020000... (20 %)
...
Hash of data verified.

Leaving...
Hard resetting via RTS pin...
========================= [SUCCESS] Took X.XX seconds =========================
```

**SUCCESS**と表示されればアップロード完了です。

---

## 📡 OTA（WiFi経由）でのアップロード（推奨手順）

### 事前準備: 初回はUSBでOTA対応コードをアップロード

OTAを使用するには、まずUSB接続でOTA機能を有効にしたコードをアップロードする必要があります。

#### サンプルコード（setup関数内に追加）

```cpp
#include <M5Unified.h>
#include <WiFi.h>
#include <ArduinoOTA.h>

const char* ssid = "your_wifi_ssid";          // WiFi SSIDに変更
const char* password = "your_wifi_password";  // WiFiパスワードに変更

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  
  // WiFi接続
  M5.Display.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    M5.Display.print(".");
  }
  
  M5.Display.println("\nWiFi connected!");
  M5.Display.print("IP: ");
  M5.Display.println(WiFi.localIP());
  
  // OTA設定
  ArduinoOTA.setHostname("esp32-0D1818");  // platformio.iniと一致
  // ArduinoOTA.setPassword("password");   // パスワード設定する場合
  
  ArduinoOTA.onStart([]() {
    M5.Display.clear();
    M5.Display.println("OTA Start");
  });
  
  ArduinoOTA.onEnd([]() {
    M5.Display.println("\nOTA End");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    M5.Display.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    M5.Display.printf("Error[%u]: ", error);
  });
  
  ArduinoOTA.begin();
  M5.Display.println("OTA Ready");
}

void loop() {
  ArduinoOTA.handle();  // 必ずloop内で呼び出す
  M5.update();
  
  // 通常のプログラム処理
}
```

### ステップ1: M5StackのIPアドレスを確認

#### 方法A: シリアルモニターで確認（推奨）

```bash
# シリアルモニターを起動
pio device monitor
```

M5Stackを再起動し、起動時に表示されるIPアドレスをメモ（例: `192.168.0.21`）

**終了方法**: `Ctrl + C`

#### 方法B: mDNSでホスト名を使用

```bash
# Windowsでpingテスト
ping esp32-0D1818.local
```

pingが通れば、ホスト名をそのまま使用できます。

### ステップ2: platformio.iniのOTA設定を確認

以下の3つの方法から1つを選択してください。

#### 方法1: ホスト名を使用（mDNS対応環境）

```ini
[env:ota]
upload_protocol = espota
upload_port = esp32-0D1818.local  # この行が有効になっているか確認
```

#### 方法2: IPアドレスを直接指定（最も確実・推奨）

```ini
[env:ota]
upload_protocol = espota
; upload_port = esp32-0D1818.local  # コメントアウト
upload_port = 192.168.0.21  # コメントを解除して実際のIPに変更
```

### ステップ3: M5StackがWiFiに接続されていることを確認

- M5Stackの電源が入っている
- WiFiに接続されている（画面にIPアドレス表示など）
- PCとM5Stackが**同じネットワーク**に接続されている

### ステップ4: コマンドでOTAアップロード

VSCodeのターミナルで以下を実行：

```bash
pio run -e ota -t upload
```

**コマンドの意味**:
- `-e ota`: OTA環境を指定
- `-t upload`: アップロードタスクを実行

### ステップ5: アップロード完了の確認

ターミナルに以下のような表示が出ます:

```
Uploading .pio\build\ota\firmware.bin
14:30:15 [DEBUG]: Options: {'esp_ip': 'esp32-0D1818.local', ...}
14:30:15 [INFO]: Starting on 0.0.0.0:xxxxx
14:30:15 [INFO]: Upload size: 1018800
Sending invitation to esp32-0D1818.local
14:30:16 [INFO]: Waiting for device...
Uploading: [====      ] 40%
...
Uploading: [==========] 100%
14:30:25 [INFO]: Waiting for result...
14:30:25 [INFO]: Result: OK
========================= [SUCCESS] Took X.XX seconds =========================
```

**SUCCESS**と表示されればアップロード完了です。

M5Stackの画面にも進捗状況が表示されます（コードに実装した場合）。

---

## 📝 便利なコマンド一覧

### ビルドのみ実行（アップロードしない）

```bash
# USB環境
pio run -e m5stack-core-esp32-16M

# OTA環境
pio run -e ota
```

コードの文法チェックや動作確認に便利です。

### シリアルモニターの起動

```bash
pio device monitor
```

**終了方法**: `Ctrl + C`

**ボーレート指定**:
```bash
pio device monitor -b 115200
```

### ビルドとアップロードを一度に実行（デフォルト）

```bash
# USB環境
pio run -e m5stack-core-esp32-16M -t upload

# OTA環境
pio run -e ota -t upload
```

### クリーンビルド

プログラムの挙動がおかしい場合:

```bash
# USB環境をクリーン
pio run -e m5stack-core-esp32-16M -t clean

# OTA環境をクリーン
pio run -e ota -t clean

# すべての環境をクリーン
pio run -t clean
```

その後、再度ビルド＆アップロード:

```bash
pio run -e m5stack-core-esp32-16M -t upload
```

### アップロード後にシリアルモニターを自動起動

```bash
# USB環境
pio run -e m5stack-core-esp32-16M -t upload && pio device monitor

# OTA環境はアップロード後にシリアルモニターは不要（WiFi経由のため）
```

### 特定のポートを指定してアップロード

platformio.iniの設定を上書きしたい場合:

```bash
# USBポートを指定
pio run -e m5stack-core-esp32-16M -t upload --upload-port COM3

# OTA IPアドレスを指定
pio run -e ota -t upload --upload-port 192.168.0.21

# OTA ホスト名を指定
pio run -e ota -t upload --upload-port esp32-0D1818.local
```

---

## 🛠️ トラブルシューティング

### USB接続でアップロードできない

#### エラー: `Failed to connect to ESP32`

**対処法**:
1. USBケーブルを抜き差しして再接続
2. M5Stackの電源ボタンを押して再起動
3. 別のUSBポートを試す
4. USBケーブルがデータ転送対応か確認（充電専用ケーブルは使用不可）

#### エラー: `Could not open port 'COM9': PermissionError`

**対処法**:
1. 他のアプリケーション（Arduino IDE、Teratermなど）でポートを使用していないか確認
2. シリアルモニターを起動していたら終了（`Ctrl + C`）
3. VSCodeを再起動

#### エラー: `A fatal error occurred: Failed to connect`

**対処法**:
1. M5Stackを**BOOTボタンを押しながら**電源ON（ダウンロードモード）
2. 再度アップロードコマンドを実行

### OTAでアップロードできない

#### エラー: `Host esp32-0D1818.local Not Found`

**対処法1: mDNSの問題を回避（IPアドレスを使用）**

```bash
# まずpingでIPアドレスを確認
ping esp32-0D1818.local

# 返ってきたIPアドレスを使用
pio run -e ota -t upload --upload-port 192.168.0.21
```

または、platformio.iniを直接編集:

```ini
[env:ota]
upload_port = 192.168.0.21  # IPアドレスに変更
```

**対処法2: WindowsでmDNSを有効化**

Windows 10以降でmDNSが動作しない場合:

1. Bonjour Serviceがインストールされているか確認
2. または、iTunes/iCloudをインストール（Bonjourが含まれる）

#### エラー: `No answer from the device`

**対処法**:

1. **M5Stackの状態確認**
   - 電源が入っているか
   - WiFiに接続されているか（画面でIPアドレスを確認）
   - プログラム内で`ArduinoOTA.handle()`を呼び出しているか

2. **ネットワーク確認**
   ```bash
   # M5StackへのPing確認
   ping 192.168.0.21
   ```
   Pingが通らない場合、PCとM5Stackが異なるネットワークにいる可能性があります。

3. **ファイアウォール確認**
   - Windowsファイアウォールでポート3232がブロックされていないか
   - セキュリティソフトが通信を遮断していないか

4. **M5Stackを再起動**
   - OTAサービスが停止している可能性があります
   - 電源ボタンで再起動

#### エラー: `Uploading: [ERROR] ERROR[11]: Bad Answer`

**対処法**:

パスワード設定の不一致が原因です。

**コードでパスワードを設定している場合**:
```cpp
ArduinoOTA.setPassword("password");
```

**platformio.iniにも追加**:
```ini
upload_flags = 
    --port=3232
    --auth=password  # 同じパスワードを設定
```

**または、パスワードを削除**:
```cpp
// ArduinoOTA.setPassword("password");  // コメントアウト
```

### ビルドエラーが発生する

#### エラー: ライブラリが見つからない

**対処法**:
```bash
# ライブラリを再インストール
pio pkg install

# または、プロジェクトを完全にクリーン
Remove-Item -Recurse -Force .pio
pio run -e m5stack-core-esp32-16M
```

#### エラー: コンパイルエラー

**対処法**:
1. エラーメッセージを確認
2. コードの文法エラーを修正
3. 使用しているライブラリのバージョンが対応しているか確認

---

## ⚡ 効率的なワークフロー

### 開発中の推奨手順

#### USB接続での開発（初期段階）

```bash
# コードを修正
# ↓
# ビルド＆アップロード＆モニター起動
pio run -e m5stack-core-esp32-16M -t upload && pio device monitor
```

**Ctrl + C**でモニターを終了 → コード修正 → 再実行

#### OTA接続での開発（設置後）

```bash
# コードを修正
# ↓
# OTAでアップロード
pio run -e ota -t upload
```

M5Stackが離れた場所にある場合や、頻繁にアップデートする場合に便利です。

### エイリアス設定（PowerShell）

頻繁に使用するコマンドをエイリアスに設定すると便利です。

PowerShellプロファイルを編集:

```powershell
notepad $PROFILE
```

以下を追加:

```powershell
# M5Stack USB アップロード
function m5usb { pio run -e m5stack-core-esp32-16M -t upload }

# M5Stack OTA アップロード
function m5ota { pio run -e ota -t upload }

# M5Stack モニター
function m5mon { pio device monitor }

# M5Stack クリーン
function m5clean { pio run -t clean }
```

保存後、新しいターミナルを開くと以下のように使用できます:

```bash
# USB アップロード
m5usb

# OTA アップロード
m5ota

# シリアルモニター
m5mon
```

---

## 📊 アップロード方式の使い分け

### USB接続を使うべき場合

- ✅ **初回のファームウェア書き込み**
- ✅ **OTA機能を有効にする場合**
- ✅ **デバイスが応答しなくなった（フリーズ）場合**
- ✅ **WiFiに接続できない場合**
- ✅ **より高速にアップロードしたい場合**（USB: ~115200bps）
- ✅ **プログラムサイズが大きい場合**
- ✅ **開発初期段階（シリアルモニターでデバッグ）**

**コマンド**:
```bash
pio run -e m5stack-core-esp32-16M -t upload
```

### OTAを使うべき場合

- ✅ **M5Stackが物理的に離れた場所にある場合**（センサーノード等）
- ✅ **USBケーブルの抜き差しを避けたい場合**
- ✅ **設置済みデバイスのメンテナンス**
- ✅ **複数のM5Stackに順次アップロードする場合**
- ✅ **筐体に組み込み済みでUSBポートにアクセスしにくい場合**
- ✅ **WiFi環境が安定している場合**

**コマンド**:
```bash
pio run -e ota -t upload
```

---

## 🎯 クイックリファレンス

### 必須コマンド

| 操作 | コマンド |
|------|----------|
| USB アップロード | `pio run -e m5stack-core-esp32-16M -t upload` |
| OTA アップロード | `pio run -e ota -t upload` |
| シリアルモニター | `pio device monitor` |
| ビルドのみ | `pio run -e <環境名>` |
| クリーンビルド | `pio run -t clean` |

### トラブル時の対応

| 問題 | 対応 |
|------|------|
| USB接続失敗 | ケーブル確認 → 再起動 → BOOTボタン |
| OTA接続失敗 | Ping確認 → IP直接指定 → 再起動 |
| ビルドエラー | クリーンビルド → ライブラリ再インストール |
| ポートが使用中 | 他アプリ終了 → モニター終了 |

---

## 📚 参考情報

### platformio.iniの設定

**USB環境**:
```ini
[env:m5stack-core-esp32-16M]
platform = espressif32
board = m5stack-core-esp32
framework = arduino
upload_speed = 115200
monitor_speed = 115200
```

**OTA環境**:
```ini
[env:ota]
platform = espressif32
board = m5stack-core-esp32
framework = arduino
upload_protocol = espota
upload_port = esp32-0D1818.local  # または IPアドレス
upload_flags = 
    --port=3232
```

### 使用ライブラリ

- M5Unified: ^0.2.8
- TinyGPSPlus-ESP32: ^0.0.2
- Adafruit SHT4x Library: ^1.0.5
- Adafruit BMP280 Library: ^2.6.8
- DallasTemperature: ^4.0.5
- Ambient ESP32 ESP8266 lib: ^1.0.5
- QMC5883LCompass: ^1.2.3

### ボード設定

- **ボード**: M5Stack Core ESP32
- **フラッシュサイズ**: 16MB
- **PSRAM**: 有効
- **モニター速度**: 115200bps
- **アップロード速度**: 115200bps

### 参考リンク

- [PlatformIO CLI公式ドキュメント](https://docs.platformio.org/en/latest/core/index.html)
- [PlatformIO コマンドリファレンス](https://docs.platformio.org/en/latest/core/userguide/index.html)
- [M5Stack公式ドキュメント](https://docs.m5stack.com/)
- [Arduino OTA公式ドキュメント](https://arduino-esp8266.readthedocs.io/en/latest/ota_updates/readme.html)
- [ESP32 Arduino Core](https://docs.espressif.com/projects/arduino-esp32/)

---

## ✨ まとめ

### 確実なアップロード方法

**USB接続**:
```bash
pio run -e m5stack-core-esp32-16M -t upload
```

**OTA接続**:
```bash
pio run -e ota -t upload
```

### 重要ポイント

1. **コマンドラインを使用する**ことでVSCode GUIのキャッシュ問題を回避
2. **環境名（-e）を明示的に指定**することで確実に意図した環境でアップロード
3. **OTAはIPアドレス指定が最も確実**（mDNSが不安定な場合）
4. **トラブル時はクリーンビルド**と**再起動**を試す
5. **シリアルモニター起動中はアップロード不可**（Ctrl+Cで終了）

コマンドライン方式により、確実で再現性の高いアップロード作業が可能になります！