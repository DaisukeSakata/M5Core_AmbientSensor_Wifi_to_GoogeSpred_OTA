// googlSpredに変更 iotdata.sakata@gmail.com spredsheet連携
// chatGPT生成のちcloudeにより修正     　 20250228 動作OK Wifiでambient接続 OTA追加ホスト名esp32-0D1818
// OTA機能追加 OTAアップロードはターミナルからコマンドラインで　pio run -e ota -t upload　　詳細はvscode-upload-guide.md
// QMC5883L磁気センサー追加、風向データ改善、表示制御機能追加
// M5stackCore + W5500LANモジュール(PoE) + Core2ポート拡張モジュール
// ENV.4(温度湿度気圧) SDA:G21 SCL:G22 
// GPS GT-502MGG-N Rx:G16 Tx:G17 9600bps
// 風速 RS-485 Tx:G15 Rx:G5 アドレス:0x02 4800bps
// 風向 RS-485 Tx:G15 Rx:G5 アドレス:0x01 4800bps
// QMC5883L 地磁気センサー I2C SDA:G21 SCL:G22
#include <ArduinoOTA.h> // OTA機能
#include <M5Unified.h>
#include <Wire.h>
#include <WiFi.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <HardwareSerial.h>
#include <TinyGPSPlus.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_BMP280.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <QMC5883LCompass.h>  // QMC5883L磁気センサー
// #include "Ambient.h"  // ambientサーバー向け追加



// Wifi設定
WiFiClient client;
// Ambient ambient; 
const char* ssid = "TP-Link_IoT_CA1D";
const char* password = "85010642";

// ambientサーバー
// unsigned int channelId = 88227; // AmbientのチャネルID  shimojima+ambient@gmail.com sakata.daisuke.job@gmail.com
// const char* writeKey = "897bd84bb0371f2f"; // ライトキー

// Google Apps ScriptのWebアプリURL
const char* gasUrl = "https://script.google.com/macros/s/AKfycbx8TPNzgzht07u5ihq26bhuNbtcR2nqSx8RjwYWYOQ5Ac-hfePniRvLIs-VZrqCwdm0/exec"; // 上記でコピーしたURLに置き換えてください

// *** UART: GPSモジュール ***
#define GPS_RX 16
#define GPS_TX 17
HardwareSerial gpsSerial(1);
TinyGPSPlus gps;

// *** I2C: ENV4（温湿度: SHT40, 気圧: BMP280） ***
Adafruit_SHT4x sht4 = Adafruit_SHT4x();
Adafruit_BMP280 bmp;

// *** I2C: QMC5883L 磁気センサー ***
QMC5883LCompass compass;

// *** 磁気センサーと風向計の軸合わせ設定 ***
// 【軸合わせの基本概念】
// - 理想状態：磁気センサーのX軸方向 = 風向センサーの応答値0（基準方向）
// - この場合は COMPASS_X_AXIS_OFFSET = 0 に設定
//
// 【軸がずれている場合の設定方法】
// 1. 風向センサーの応答値0の方向を確認（通常は北向き）
// 2. 磁気センサーのX軸がどの風向センサー応答値を向いているかを確認
// 3. そのずれ量を COMPASS_X_AXIS_OFFSET に設定
//
// 【設定例】
// - 磁気センサーX軸が風向センサーの「2」（東）を向いている場合：COMPASS_X_AXIS_OFFSET = 2
// - 磁気センサーX軸が風向センサーの「6」（西）を向いている場合：COMPASS_X_AXIS_OFFSET = 6
//
// 【応答値と方位の対応】
// 0=North, 1=NorthEast, 2=East, 3=SouthEast, 4=South, 5=SouthWest, 6=West, 7=NorthWest
//
#define COMPASS_X_AXIS_OFFSET 0  // 磁気センサーX軸と風向センサー応答値0のずれ量（0-7）
                                 // 初期値0 = X軸が風向センサーの応答値0と一致している状態

// *** RS485: 風速・風向 ***
#define RS485_TX 15
#define RS485_RX 5
#define RS485_BAUDRATE 4800
HardwareSerial rs485(2);
uint8_t windSpeedRequest[] = {0x02, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x39};
uint8_t windDirectionRequest[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};

// 修正: 方位の配列を正しく定義
const char* directions[] = {"North", "NorthEast", "East", "SouthEast", "South", "SouthWest", "West", "NorthWest"};

// *** OneWire: DS18B20（水温） ***
#define ONE_WIRE_BUS 0
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// センサーの状態を追跡するフラグ
bool sht4_ok = false;
bool bmp_ok = false;
bool ds18b20_ok = false;
bool compass_ok = false;

// 風向データ管理用構造体とバッファ
#define WIND_SAMPLES_MAX 50  // 10秒間での最大サンプル数
struct WindDirectionData {
    int values[WIND_SAMPLES_MAX];
    unsigned long timestamps[WIND_SAMPLES_MAX];
    int count;
    int currentIndex;
};

WindDirectionData windDirBuffer;
bool displayOn = true;  // 画面表示のON/OFF状態

// 風向データバッファを初期化
void initWindDirectionBuffer() {
    windDirBuffer.count = 0;
    windDirBuffer.currentIndex = 0;
    memset(windDirBuffer.values, -1, sizeof(windDirBuffer.values));
    memset(windDirBuffer.timestamps, 0, sizeof(windDirBuffer.timestamps));
}

// 風向取得の前方宣言
int getWindDirection();

// 10秒間の風向データを収集する関数
bool collectWindDirectionData(int samples[], int maxSamples, int* actualSamples) {
    *actualSamples = 0;
    unsigned long startTime = millis();
    unsigned long lastSampleTime = 0;
    
    Serial.println("Starting 10-second wind direction sampling...");
    
    while (millis() - startTime < 10000 && *actualSamples < maxSamples) { // 10秒間
        unsigned long currentTime = millis();
        
        // 約200ms間隔でサンプリング（4800bpsに適した頻度）
        if (currentTime - lastSampleTime >= 200) {
            int windDir = getWindDirection();
            if (windDir >= 0 && windDir < 8) {
                samples[*actualSamples] = windDir;
                (*actualSamples)++;
                Serial.printf("Sample %d: %d (%s)\n", *actualSamples, windDir, directions[windDir]);
            }
            lastSampleTime = currentTime;
        }
        
        delay(50); // 短い待機
    }
    
    Serial.printf("Wind sampling completed: %d samples in 10 seconds\n", *actualSamples);
    return (*actualSamples >= 3); // 最低3サンプル必要
}

// 円形平均を計算（風向用）
int calculateCircularMean(int values[], int count) {
    if (count == 0) return -1;
    
    double sumSin = 0.0;
    double sumCos = 0.0;
    
    for (int i = 0; i < count; i++) {
        double angle = values[i] * 45.0 * PI / 180.0; // 0-7を0-315度に変換
        sumSin += sin(angle);
        sumCos += cos(angle);
    }
    
    double meanAngle = atan2(sumSin / count, sumCos / count) * 180.0 / PI;
    if (meanAngle < 0) meanAngle += 360.0;
    
    // 360度を8方位（0-7）に変換
    return (int)((meanAngle + 22.5) / 45.0) % 8;
}

// 風向のばらつき指標を計算（標準偏差ベース、0-100スケール）
float calculateWindDirectionVariability(int values[], int count, int meanDir) {
    if (count <= 1) return 0.0;
    
    double sumSquaredDiff = 0.0;
    
    for (int i = 0; i < count; i++) {
        // 円形距離を計算
        int diff = abs(values[i] - meanDir);
        if (diff > 4) diff = 8 - diff; // 円形での最短距離
        sumSquaredDiff += diff * diff;
    }
    
    double variance = sumSquaredDiff / count;
    double stddev = sqrt(variance);
    
    // 0-100スケールに正規化（最大標準偏差は約2.83）
    return min(100.0, (stddev / 2.83) * 100.0);
}

// 磁気センサーの軸合わせと偏角補正を適用して風向を計算
int correctWindDirectionWithCompass(int rawWindDir, float magneticBearing) {
    if (rawWindDir < 0 || rawWindDir > 7) return rawWindDir;
    
    // 1. 風向計の生データを角度に変換（0=North=0度, 1=NorthEast=45度, ...）
    float windDirAngle = rawWindDir * 45.0;
    
    // 2. QMC5883Lの磁気方位を取得（0-360度）
    // 磁気センサーのX軸オフセット補正を適用
    // オフセット分だけ磁気方位を回転させて、X軸を風向センサーの基準方向（応答値0）に合わせる
    float compassCorrectedAngle = magneticBearing - (COMPASS_X_AXIS_OFFSET * 45.0);
    if (compassCorrectedAngle < 0) compassCorrectedAngle += 360.0;
    if (compassCorrectedAngle >= 360.0) compassCorrectedAngle -= 360.0;
    
    // 3. 磁気偏角補正を適用（日本では約-7度西偏）
    float trueNorthAngle = compassCorrectedAngle - 7.0;  // 磁気偏角補正
    if (trueNorthAngle < 0) trueNorthAngle += 360.0;
    
    // 4. 風向計の設置方位誤差を補正
    // 磁気センサーで測定した真北方向から風向計の向きを補正
    float deviceOrientationError = trueNorthAngle;
    float correctedWindAngle = windDirAngle - deviceOrientationError;
    if (correctedWindAngle < 0) correctedWindAngle += 360.0;
    if (correctedWindAngle >= 360.0) correctedWindAngle -= 360.0;
    
    // 5. 補正された角度を8方位（0-7）に変換
    return (int)((correctedWindAngle + 22.5) / 45.0) % 8;
}

// RS485でデータ送信と受信を行う関数
bool sendAndReceive(uint8_t* request, size_t requestSize, uint8_t* response, size_t responseSize) {
    while (rs485.available()) {
        rs485.read();
    }
    
    rs485.write(request, requestSize);
    delay(100);
    memset(response, 0, responseSize);
    size_t index = 0;
    unsigned long timeout = millis() + 500;

    while (millis() < timeout) {
        if (rs485.available()) {
            response[index++] = rs485.read();
            if (index >= responseSize) break;
        }
    }
    return index > 0;
}

// 風速取得
float getWindSpeed() {
    uint8_t response[7];
    if (sendAndReceive(windSpeedRequest, sizeof(windSpeedRequest), response, sizeof(response))) {
        Serial.print("Wind Speed Response: ");
        for (int i = 0; i < 5 && i < sizeof(response); i++) {
            Serial.printf("%02X ", response[i]);
        }
        Serial.println();
        
        uint16_t rawSpeed = (response[3] << 8) | response[4];
        return rawSpeed / 10.0;
    }
    return -999.0;
}

// 風向取得
int getWindDirection() {
    uint8_t response[7];
    if (sendAndReceive(windDirectionRequest, sizeof(windDirectionRequest), response, sizeof(response))) {
        Serial.print("Wind Direction Response: ");
        for (int i = 0; i < 5 && i < sizeof(response); i++) {
            Serial.printf("%02X ", response[i]);
        }
        Serial.println();
        
        int direction = response[4];
        return (direction < 8) ? direction : -1;
    }
    return -1;
}

// 地磁気方位を取得
float getMagneticBearing() {
    if (!compass_ok) return -999.0;
    
    compass.read();
    int azimuth = compass.getAzimuth();
    return (float)azimuth;
}

void setupOTA() {
    // ホスト名設定
    ArduinoOTA.setHostname("esp32-0D1818");
    
    // OTAポート設定（デフォルトは3232）
    // ArduinoOTA.setPort(3232);
    
    // OTA開始時のコールバック
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        } else { // U_SPIFFS
            type = "filesystem";
        }
        
        Serial.println("Start updating " + type);
        M5.Lcd.clear();
        M5.Lcd.setTextSize(2);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.println("OTA Update");
        M5.Lcd.println("Starting...");
        M5.Lcd.printf("Type: %s\n", type.c_str());
    });
    
    // OTA終了時のコールバック
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
        M5.Lcd.clear();
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.println("OTA Update");
        M5.Lcd.println("Completed!");
        M5.Lcd.println("Rebooting...");
        delay(1000);
    });
    
    // OTA進行状況のコールバック
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static unsigned int lastProgress = 0;
        unsigned int currentProgress = (progress / (total / 100));
        
        // 5%ごとに表示更新（頻繁な更新を避ける）
        if (currentProgress - lastProgress >= 5) {
            Serial.printf("Progress: %u%%\r", currentProgress);
            
            M5.Lcd.fillRect(0, 60, 320, 40, BLACK);
            M5.Lcd.setCursor(0, 60);
            M5.Lcd.printf("Progress: %u%%", currentProgress);
            
            // プログレスバー表示
            int barWidth = (320 * currentProgress) / 100;
            M5.Lcd.fillRect(0, 100, barWidth, 20, GREEN);
            M5.Lcd.fillRect(barWidth, 100, 320 - barWidth, 20, BLACK);
            
            lastProgress = currentProgress;
        }
    });
    
    // OTAエラー時のコールバック
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        M5.Lcd.fillRect(0, 140, 320, 60, BLACK);
        M5.Lcd.setCursor(0, 140);
        M5.Lcd.setTextColor(RED, BLACK);
        M5.Lcd.println("OTA Error!");
        
        if (error == OTA_AUTH_ERROR) {
            Serial.println("Auth Failed");
            M5.Lcd.println("Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
            Serial.println("Begin Failed");
            M5.Lcd.println("Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
            Serial.println("Connect Failed");
            M5.Lcd.println("Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
            Serial.println("Receive Failed");
            M5.Lcd.println("Receive Failed");
        } else if (error == OTA_END_ERROR) {
            Serial.println("End Failed");
            M5.Lcd.println("End Failed");
        }
        
        M5.Lcd.setTextColor(WHITE, BLACK);
        delay(3000);
    });
    
    // パスワード設定（セキュリティ向上のため）
    // ArduinoOTA.setPassword("your_password_here");
    
    // MD5ハッシュパスワード設定（より安全）
    // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");
    
    ArduinoOTA.begin();
    
    Serial.println("OTA Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.printf("Hostname: %s.local\n", ArduinoOTA.getHostname().c_str());
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("Initializing...");

    // 風向データバッファ初期化
    initWindDirectionBuffer();

    // Wifi接続
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.print("WiFi connected\r\nIP address: ");
    Serial.println(WiFi.localIP());

    // OTA初期化（拡張版）
    setupOTA();
    M5.Lcd.printf("OTA: %s.local\n", ArduinoOTA.getHostname().c_str());

    // ambient初期化
    // ambient.begin(channelId, writeKey, &client);
    // M5.Lcd.println("Ambient connected");

    // GPS 初期化
    gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
    Serial.println("GPS initialized");
    M5.Lcd.println("GPS initialized");

    // ENV4 初期化
    sht4_ok = sht4.begin();
    if (!sht4_ok) {
        Serial.println("SHT40 not found!");
        M5.Lcd.println("SHT40 not found!");
    } else {
        Serial.println("SHT40 initialized");
        M5.Lcd.println("SHT40 initialized");
    }
    
    bmp_ok = bmp.begin(0x76);
    if (!bmp_ok) {
        Serial.println("BMP280 not found!");
        M5.Lcd.println("BMP280 not found!");
    } else {
        Serial.println("BMP280 initialized");
        M5.Lcd.println("BMP280 initialized");
    }

    // QMC5883L 磁気センサー初期化
    compass.init();
    compass.setSmoothing(10, true);
    // 簡単な接続テスト
    delay(100);  // センサー安定化待ち
    compass.read();
    int testAzimuth = compass.getAzimuth();
    if (testAzimuth >= 0 && testAzimuth <= 360) {
        compass_ok = true;
        Serial.println("QMC5883L initialized");
        M5.Lcd.println("QMC5883L initialized");
    } else {
        compass_ok = false;
        Serial.println("QMC5883L not found!");
        M5.Lcd.println("QMC5883L not found!");
    }

    // RS485 初期化
    rs485.begin(RS485_BAUDRATE, SERIAL_8N1, RS485_RX, RS485_TX);
    Serial.println("RS485 initialized");
    M5.Lcd.println("RS485 initialized");

    // DS18B20 初期化
    sensors.begin();
    sensors.requestTemperatures();
    if (sensors.getTempCByIndex(0) == DEVICE_DISCONNECTED_C) {
        Serial.println("DS18B20 not found!");
        M5.Lcd.println("DS18B20 not found!");
        ds18b20_ok = false;
    } else {
        Serial.println("DS18B20 initialized");
        M5.Lcd.println("DS18B20 initialized");
        ds18b20_ok = true;
    }
    
    Serial.println("Setup complete");
    delay(2000);
}

void loop() {
    ArduinoOTA.handle();
    M5.update();

    // Aボタンで画面表示のON/OFF切り替え
    if (M5.BtnA.wasPressed()) {
        displayOn = !displayOn;
        if (!displayOn) {
            M5.Lcd.clear();
            M5.Lcd.setTextColor(WHITE, BLACK);
            M5.Lcd.setCursor(0, 0);
            M5.Lcd.setTextSize(2);
            M5.Lcd.println("Display OFF");
            M5.Lcd.println("Press A to turn ON");
        }
        Serial.printf("Display turned %s\n", displayOn ? "ON" : "OFF");
    }

    // Bボタンでネットワーク情報表示
    if (M5.BtnB.wasPressed()) {
        M5.Lcd.clear();
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.setTextSize(2);
        M5.Lcd.println("=== Network Info ===");
        M5.Lcd.print("IP: ");
        M5.Lcd.println(WiFi.localIP());
        M5.Lcd.print("Host: ");
        M5.Lcd.println(ArduinoOTA.getHostname());
        M5.Lcd.print("SSID: ");
        M5.Lcd.println(WiFi.SSID());
        M5.Lcd.print("RSSI: ");
        M5.Lcd.print(WiFi.RSSI());
        M5.Lcd.println(" dBm");
        M5.Lcd.println("\nPress C to continue");
        
        // Cボタンが押されるまで待機
        while (!M5.BtnC.wasPressed()) {
            M5.update();
            ArduinoOTA.handle(); // OTAは継続
            delay(50);
        }
    }
    static unsigned long lastMainLoop = 0;
    unsigned long currentTime = millis();
    
    // メインデータ処理（5分間隔）
    if (currentTime - lastMainLoop >= 300000) { // 5分
        lastMainLoop = currentTime;
        
        if (displayOn) {
            M5.Lcd.clear();
            M5.Lcd.setTextColor(WHITE, BLACK);
            M5.Lcd.setCursor(0, 0);
            M5.Lcd.setTextSize(2);
        }

        // GPSデータ取得
        bool gps_valid = false;
        unsigned long start_time = millis();
        
        while (gpsSerial.available() && (millis() - start_time < 1000)) {
            if (gps.encode(gpsSerial.read())) {
                gps_valid = true;
            }
        }

        // GPSデータ処理・表示
        if (gps_valid && gps.time.isValid() && gps.location.isValid()) {
            int utcHours = gps.time.hour();
            int utcMinutes = gps.time.minute();
            int utcSeconds = gps.time.second();
            int jstHours = (utcHours + 9) % 24;
            
            if (displayOn) {
                M5.Lcd.printf("UTC: %02d:%02d:%02d\n", utcHours, utcMinutes, utcSeconds);
                M5.Lcd.printf("JST: %02d:%02d:%02d\n", jstHours, utcMinutes, utcSeconds);
            }
            
            if (gps.location.isValid()) {
                double latitude = gps.location.lat();
                double longitude = gps.location.lng();
                double altitude = gps.altitude.meters();
                int satellites = gps.satellites.value();
                
                if (displayOn) {
                    M5.Lcd.printf("Lat: %.5f\n", latitude);
                    M5.Lcd.printf("Lon: %.5f\n", longitude);
                    M5.Lcd.printf("Alt: %.1fm Sat:%d\n", altitude, satellites);
                }
                
                Serial.printf("[UTC] %02d:%02d:%02d [JST] %02d:%02d:%02d\n", 
                            utcHours, utcMinutes, utcSeconds, jstHours, utcMinutes, utcSeconds);
                Serial.printf("Lat: %.5f, Lon: %.5f, Alt: %.1fm, Sat: %d\n",
                            latitude, longitude, altitude, satellites);
            }
        } else {
            if (displayOn) M5.Lcd.println("GPS: No valid data");
            Serial.println("GPS: No valid data");
        }
        
        // 環境センサ取得
        float temperature = -999.0;
        float humidity = -999.0;
        float pressure = -999.0;
        
        if (sht4_ok) {
            sensors_event_t humidityEvent, tempEvent;
            sht4.getEvent(&humidityEvent, &tempEvent);
            temperature = tempEvent.temperature;
            humidity = humidityEvent.relative_humidity;
            
            if (displayOn) {
                M5.Lcd.printf("T:%.1fC H:%.1f%%\n", temperature, humidity);
            }
            Serial.printf("Temp: %.1fC, Hum: %.1f%%, ", temperature, humidity);
        } else {
            if (displayOn) M5.Lcd.println("Temp/Hum: Error");
            Serial.print("Temp/Hum: Error, ");
        }
        
        if (bmp_ok) {
            pressure = bmp.readPressure() / 100.0F;
            if (displayOn) M5.Lcd.printf("P:%.1fhPa\n", pressure);
            Serial.printf("Pres: %.1fhPa\n", pressure);
        } else {
            if (displayOn) M5.Lcd.println("Pressure: Error");
            Serial.println("Pressure: Error");
        }

        // 風速取得
        float windSpeed = getWindSpeed();

        // 磁気方位を測定（Ambient送信時）
        float magneticBearing = -999.0;
        if (compass_ok) {
            magneticBearing = getMagneticBearing();
            Serial.printf("Magnetic Bearing: %.1f degrees\n", magneticBearing);
            if (displayOn) M5.Lcd.printf("Mag:%.0fdeg\n", magneticBearing);
        }

        // 風向データを10秒間収集（Ambient送信時）
        int windSamples[WIND_SAMPLES_MAX];
        int sampleCount = 0;
        int avgWindDir = -1;
        int correctedWindDir = -1;
        float windVariability = 0.0;
        
        if (displayOn) M5.Lcd.println("Sampling wind...");
        Serial.println("Starting wind direction measurement...");
        
        if (collectWindDirectionData(windSamples, WIND_SAMPLES_MAX, &sampleCount)) {
            avgWindDir = calculateCircularMean(windSamples, sampleCount);
            windVariability = calculateWindDirectionVariability(windSamples, sampleCount, avgWindDir);
            
            // 地磁気センサーによる方位補正を適用
            if (compass_ok && magneticBearing > -900.0) {
                correctedWindDir = correctWindDirectionWithCompass(avgWindDir, magneticBearing);
                Serial.printf("Raw avg dir: %d, Magnetic bearing: %.1f, Corrected dir: %d\n", 
                             avgWindDir, magneticBearing, correctedWindDir);
            } else {
                correctedWindDir = avgWindDir; // 磁気センサーが無効な場合は生データを使用
            }
        }

        // 風向・風速表示
        if (windSpeed > -900.0 && correctedWindDir >= 0) {
            if (displayOn) {
                M5.Lcd.printf("Wind:%.1fm/s %s\n", windSpeed, directions[correctedWindDir]);
                M5.Lcd.printf("Dir var:%.0f%% n:%d\n", windVariability, sampleCount);
            }
            Serial.printf("Wind: %.1fm/s, Dir: %s(%d), Var: %.1f%%, Samples: %d\n", 
                         windSpeed, directions[correctedWindDir], correctedWindDir, windVariability, sampleCount);
        } else {
            if (displayOn) M5.Lcd.println("Wind: Error");
            Serial.println("Wind: Error");
        }

        // 水温取得
        float waterTemp = -999.0;
        if (ds18b20_ok) {
            sensors.requestTemperatures();
            waterTemp = sensors.getTempCByIndex(0);
            
            if (waterTemp != DEVICE_DISCONNECTED_C) {
                if (displayOn) M5.Lcd.printf("Water:%.1fC\n", waterTemp);
                Serial.printf("Water Temp: %.1fC\n", waterTemp);
            } else {
                if (displayOn) M5.Lcd.println("Water: Error");
                Serial.println("Water Temp: Error");
                ds18b20_ok = false;
            }
        } else {
            if (displayOn) M5.Lcd.println("Water: No sensor");
            Serial.println("Water Temp: No sensor");
            
            sensors.begin();
            sensors.requestTemperatures();
            if (sensors.getTempCByIndex(0) != DEVICE_DISCONNECTED_C) {
                ds18b20_ok = true;
            }
        }

        // Ambient送信
        // ambient.set(1, temperature);
        // ambient.set(2, humidity);
        // ambient.set(3, pressure);
        // ambient.set(4, windSpeed);
        // ambient.set(5, correctedWindDir >= 0 ? correctedWindDir : -999);  // 補正された風向
        // ambient.set(6, windVariability);  // 風向変動指標
        // ambient.set(7, waterTemp);

        // ambient.send();
        // Serial.println("Data sent to Ambient");
        
        // DynamicJsonDocument doc(256);
        JsonDocument doc; // 
        doc["temperature"] = temperature;
        doc["humidity"] = humidity;
        doc["pressure"] = pressure;
        doc["windspeed"] = windSpeed;
        doc["correctedWindDir"] = correctedWindDir;
        doc["windVariability"] = windVariability;
        doc["waterTemp"] = waterTemp;

        String jsonString;
        serializeJson(doc, jsonString);

        if (WiFi.status() == WL_CONNECTED) {
            HTTPClient http;
            http.begin(gasUrl);
            http.addHeader("Content-Type", "application/json");

            int httpResponseCode = http.POST(jsonString);

            if (httpResponseCode > 0) {
                M5.Lcd.printf("HTTP Response code: %d\n", httpResponseCode);
                Serial.printf("HTTP Response code: %d\n", httpResponseCode);

                // レスポンスコードが302 (Moved Temporarily) の場合は、サーバーからのHTMLレスポンスを表示しない
                if (httpResponseCode != 302) { // ★ここを数値の 302 に修正しました★
                String response = http.getString();
                M5.Lcd.printf("Server Response: %s\n", response.c_str());
                Serial.printf("Server Response: %s\n", response.c_str());
                } else {
                M5.Lcd.println("Server responded with 302 Redirect (Data sent).");
                Serial.println("Server responded with 302 Redirect (Data sent).");
                }
            } else {
                M5.Lcd.printf("Error code: %d\n", httpResponseCode);
                M5.Lcd.printf("Error: %s\n", http.errorToString(httpResponseCode).c_str());
                Serial.printf("Error code: %d\n", httpResponseCode);
                Serial.printf("Error: %s\n", http.errorToString(httpResponseCode).c_str());
            }
            http.end();
        } else {
            M5.Lcd.println("WiFi Disconnected. Reconnecting...");
            Serial.println("WiFi Disconnected. Reconnecting...");
            WiFi.begin(ssid, password);
            while (WiFi.status() != WL_CONNECTED) {
                delay(500);
                M5.Lcd.print(".");
                Serial.print(".");
            }
            M5.Lcd.println("\nWiFi Reconnected!");
            Serial.println("\nWiFi Reconnected!");
        }
        if (displayOn) {
            M5.Lcd.println("Data sent!");
        }
    }
    
    delay(100); // 短いループ遅延
}