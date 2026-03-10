#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_DPS310.h>

// ハードウェアピン定義
#define TFT_CLK   10
#define TFT_DIN   11
#define TFT_DC    12
#define TFT_CS    13
#define TFT_RST   14
#define TFT_BL    15

#define I2C_SDA   16
#define I2C_SCL   17

// ★修正点1：SPI1インスタンスの明示的利用
// GP10とGP11は「SPI1」コントローラに属するため、標準のSPI(SPI0)ではなく、
// SPI1のアドレス(&SPI1)をライブラリに渡すことでハードウェアルーティングを整合させます。
Adafruit_ST7789 tft = Adafruit_ST7789(&SPI1, TFT_CS, TFT_DC, TFT_RST);
Adafruit_DPS310 dps;

const float SEA_LEVEL_PRESSURE = 1013.25;

void setup() {
  // まずUSBシリアル（TinyUSBスタック）を起動し、PCからの要求に応答できる状態にします
  Serial.begin(115200);

  // ★修正点2：USB列挙(Enumeration)タイムアウトの回避
  // 周辺ICのコンデンサ充電やPOR解除を待つための2秒間ですが、
  // 長時間CPUをブロックするとWindowsのUSB認識プロセスがタイムアウトします。
  // yield()を定期的に呼ぶことで、バックグラウンドのUSB割り込み処理を継続させます。
  for (int i = 0; i < 20; i++) {
    delay(100);
    yield();
  }

  // --- デバッグ用LEDの初期化 ---
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // ステップ0: 起動直後に点灯

  // バックライト回路の初期化
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH); 

  // I2C0 (DPS310用)
  // GP16/17はI2C0ペリフェラルに属するため、Wireインスタンスで正常にルーティングされます
  Wire.setSDA(I2C_SDA);
  Wire.setSCL(I2C_SCL);
  Wire.begin();

  // SPI1 (ST7789用)
  // ここでも必ず SPI1 インスタンスに対してピン設定と初期化を行います
  SPI1.setSCK(TFT_CLK); // GP10
  SPI1.setTX(TFT_DIN);  // GP11
  SPI1.begin();

  // ST7789の初期化
  tft.init(240, 240);
  
  // ★修正点3：シグナル・インテグリティの確保
  // 40MHz等の高周波は、ブレッドボードやジャンパワイヤの寄生インダクタンス・浮遊容量により
  // 波形が崩れ（リンギング）、コマンドが認識されなくなる原因となります。
  // 安定動作のための閾値として、安全な20MHzに落としています。
  tft.setSPISpeed(20000000); 
  
  tft.setRotation(2); 
  tft.fillScreen(ST77XX_BLACK);
  
  digitalWrite(LED_BUILTIN, LOW); // ステップ1: 液晶の初期化まで完了したら消灯
  delay(500);

  // DPS310センサの初期化
  if (!dps.begin_I2C(0x77, &Wire)) {
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(10, 10);
    tft.println("DPS310 Init Error!");
    
    // エラー時はLEDを高速点滅（0.1秒周期）させて知らせる
    while (1) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(100);
    }
  }

  // ステップ2: センサの初期化も無事完了したら、LEDを点灯（正常起動完了）
  digitalWrite(LED_BUILTIN, HIGH); 

  dps.configurePressure(DPS310_64HZ, DPS310_64SAMPLES);
  dps.configureTemperature(DPS310_64HZ, DPS310_64SAMPLES);

  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Env Monitor");
  tft.drawLine(10, 30, 230, 30, ST77XX_CYAN);
}

void loop() {
  sensors_event_t temp_event, pressure_event;

  if (dps.getEvents(&temp_event, &pressure_event)) {
    float pressure = pressure_event.pressure;
    float temperature = temp_event.temperature;
    float altitude = 44330.0 * (1.0 - pow(pressure / SEA_LEVEL_PRESSURE, 0.1903));

    tft.fillRect(10, 50, 230, 190, ST77XX_BLACK);
    tft.setTextSize(2);

    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 50);
    tft.println("Pressure:");
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(30, 80);
    tft.print(pressure, 2);
    tft.println(" hPa");

    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 120);
    tft.println("Temperature:");
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(30, 150);
    tft.print(temperature, 2);
    tft.println(" C");

    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 190);
    tft.println("Altitude:");
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(30, 220);
    tft.print(altitude, 1);
    tft.println(" m");
  }
  
  // ループ稼働中はLEDをゆっくり点滅（動作サイン）
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  delay(1000);
}