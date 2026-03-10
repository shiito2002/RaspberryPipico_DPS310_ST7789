#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_DPS310.h>

// ==========================================
// ハードウェアピン定義
// ==========================================
#define TFT_CLK   10
#define TFT_DIN   11
#define TFT_DC    12
#define TFT_CS    13
#define TFT_RST   14
#define TFT_BL    15

#define I2C_SDA   16
#define I2C_SCL   17

// ==========================================
// インスタンスと定数
// ==========================================
Adafruit_ST7789 tft = Adafruit_ST7789(&SPI1, TFT_CS, TFT_DC, TFT_RST);
Adafruit_DPS310 dps;

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240

const float SEA_LEVEL_PRESSURE = 1013.25;

// カスタムカラー定義 (RGB565フォーマット)
#define ST77XX_LIGHTGREY 0xC618 
#define ST77XX_DARKGREY  0x4208 

// ==========================================
// ダブルバッファリング用の仮想キャンバス
// RP2040の豊富なRAMを活用し、描画のちらつきを完全に排除します
// ==========================================
GFXcanvas16 numCanvas(240, 90);   // 数値エリア用 (約43KB)
GFXcanvas16 graphCanvas(240, 110);// グラフエリア用 (約52KB)

// ==========================================
// データ記録用バッファと統計変数
// ==========================================
float pressureHistory[SCREEN_WIDTH];
int historyIndex = 0;      
bool bufferFilled = false; 

float minTemp = 1000.0;
float maxTemp = -1000.0;

unsigned long lastUpdate = 0;
// ★ ご要望に合わせて更新頻度を 2Hz (500ms) に変更
const unsigned long UPDATE_INTERVAL = 500; 

void setup() {
  Serial.begin(115200);

  // USB列挙タイムアウト回避と周辺ICのPOR待機
  for (int i = 0; i < 20; i++) {
    delay(100);
    yield();
  }

  // デバッグ用LEDとバックライト
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH); 

  // I2C初期化
  Wire.setSDA(I2C_SDA);
  Wire.setSCL(I2C_SCL);
  Wire.begin();

  // SPI1初期化
  SPI1.setSCK(TFT_CLK);
  SPI1.setTX(TFT_DIN);
  SPI1.begin();

  // ST7789初期化
  tft.init(240, 240);
  tft.setSPISpeed(20000000); 
  tft.setRotation(2); 
  tft.fillScreen(ST77XX_BLACK);
  
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);

  // DPS310センサの初期化
  if (!dps.begin_I2C(0x77, &Wire)) {
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(10, 10);
    tft.println("DPS310 Init Error!");
    while (1) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(100);
    }
  }

  digitalWrite(LED_BUILTIN, HIGH); 

  // センサ設定: 64Hzレート, 128回オーバーサンプリング
  dps.configurePressure(DPS310_64HZ, DPS310_128SAMPLES);
  dps.configureTemperature(DPS310_64HZ, DPS310_128SAMPLES);

  // バッファの初期化（ゼロクリア）
  for (int i = 0; i < SCREEN_WIDTH; i++) {
    pressureHistory[i] = 0.0;
  }

  // 静的なUIフレームの描画 (再描画されない背景要素)
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.setCursor(10, 5);
  tft.println("Environment Monitor");
  tft.drawLine(10, 25, 230, 25, ST77XX_CYAN);
  
  tft.drawLine(0, 125, 240, 125, ST77XX_DARKGREY);
}

void loop() {
  sensors_event_t temp_event, pressure_event;

  // 2Hzの周期でサンプリングと描画を実行
  if (millis() - lastUpdate >= UPDATE_INTERVAL) {
    lastUpdate = millis();

    if (dps.getEvents(&temp_event, &pressure_event)) {
      float temp = temp_event.temperature;
      float pressure = pressure_event.pressure;
      float altitude = 44330.0 * (1.0 - pow(pressure / SEA_LEVEL_PRESSURE, 0.1903));

      // 1. 統計データの更新
      if (temp < minTemp) minTemp = temp;
      if (temp > maxTemp) maxTemp = temp;

      // 2. リングバッファへの記録
      pressureHistory[historyIndex] = pressure;
      historyIndex++;
      if (historyIndex >= SCREEN_WIDTH) {
        historyIndex = 0;
        bufferFilled = true;
      }

      // 3. 数値情報の描画更新
      updateNumberDisplay(temp, pressure, altitude, minTemp, maxTemp);

      // 4. グラフの描画更新
      updateGraphDisplay();

      // 動作インジケータ（LEDトグル）
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
  }
}

// ==========================================
// 数値描画モジュール (画面上半部)
// ==========================================
void updateNumberDisplay(float temp, float pressure, float altitude, float minT, float maxT) {
  // キャンバスのメモリ上を黒でクリア
  numCanvas.fillScreen(ST77XX_BLACK);

  numCanvas.setTextSize(2);

  // 気圧と高度
  numCanvas.setTextColor(ST77XX_WHITE);
  numCanvas.setCursor(10, 5);
  numCanvas.print("P:");
  numCanvas.setTextColor(ST77XX_YELLOW);
  numCanvas.print(pressure, 2);
  numCanvas.print("hPa");

  numCanvas.setTextColor(ST77XX_LIGHTGREY);
  numCanvas.setCursor(10, 25);
  numCanvas.print("A:");
  numCanvas.print(altitude, 1);
  numCanvas.print("m");

  // 温度
  numCanvas.setTextColor(ST77XX_WHITE);
  numCanvas.setCursor(10, 50);
  numCanvas.print("T:");
  numCanvas.setTextColor(ST77XX_ORANGE);
  numCanvas.print(temp, 2);
  numCanvas.print("C");

  // 温度Min/Max 
  numCanvas.setTextSize(1);
  numCanvas.setTextColor(ST77XX_CYAN);
  numCanvas.setCursor(10, 75);
  numCanvas.print("Min: ");
  numCanvas.print(minT, 2);
  numCanvas.print("C  Max: ");
  numCanvas.print(maxT, 2);
  numCanvas.print("C");

  // ★ ダブルバッファリング転送
  // メモリ上で完成した画像を、ディスプレイの所定位置(Y=30)へ一括転送
  tft.drawRGBBitmap(0, 30, numCanvas.getBuffer(), 240, 90);
}

// ==========================================
// グラフ描画モジュール (画面下半部)
// ==========================================
void updateGraphDisplay() {
  // キャンバスのメモリ上を黒でクリア
  graphCanvas.fillScreen(ST77XX_BLACK);

  int dataCount = bufferFilled ? SCREEN_WIDTH : historyIndex;
  if (dataCount < 2) return; 

  // オートスケールのための最小・最大探索
  float minP = pressureHistory[0];
  float maxP = pressureHistory[0];
  for (int i = 0; i < dataCount; i++) {
    if (pressureHistory[i] < minP) minP = pressureHistory[i];
    if (pressureHistory[i] > maxP) maxP = pressureHistory[i];
  }

  // ★ オートスケール最小スパンの設定
  // DPS310の高感度(相対精度±0.06hPa)を活かし、微気圧変動を視覚化するため
  // 最小スパンを0.1hPaに設定します。
  float span = maxP - minP;
  if (span < 0.1) { 
    span = 0.1;
    float mid = (maxP + minP) / 2.0;
    minP = mid - 0.05;
    maxP = mid + 0.05;
  }

  const int GRAPH_HEIGHT = 110;
  
  // ★ ロールモード描画のためのX座標オフセット
  // 常に最新データが画面の右端(X=239)に来るようにシフトする
  int offsetX = SCREEN_WIDTH - dataCount;

  int startIdx = bufferFilled ? historyIndex : 0;
  
  for (int x = 0; x < dataCount - 1; x++) {
    int idx1 = (startIdx + x) % SCREEN_WIDTH;
    int idx2 = (startIdx + x + 1) % SCREEN_WIDTH;

    // 線形写像による物理量からYピクセル座標への変換
    int y1 = (GRAPH_HEIGHT - 1) - (int)(((pressureHistory[idx1] - minP) / span) * (GRAPH_HEIGHT - 1));
    int y2 = (GRAPH_HEIGHT - 1) - (int)(((pressureHistory[idx2] - minP) / span) * (GRAPH_HEIGHT - 1));

    graphCanvas.drawLine(offsetX + x, y1, offsetX + x + 1, y2, ST77XX_GREEN);
  }

  // スケール軸の値（Max/Min）をオーバーレイ
  graphCanvas.setTextSize(1);
  graphCanvas.setTextColor(ST77XX_LIGHTGREY);
  graphCanvas.setCursor(2, 2);
  graphCanvas.print(maxP, 2);
  
  graphCanvas.setCursor(2, GRAPH_HEIGHT - 10);
  graphCanvas.print(minP, 2);

  // ★ ダブルバッファリング転送
  // メモリ上で完成した画像を、ディスプレイの所定位置(Y=130)へ一括転送
  tft.drawRGBBitmap(0, 130, graphCanvas.getBuffer(), 240, 110);
}