#include <Arduino.h>
#include <M5Unified.h>
#include <BluetoothSerial.h>
#include <algorithm>

BluetoothSerial SerialBT;
M5Canvas canvas(&M5.Display);

// ==========================================
// 環境に合わせて書き換えてください
// ==========================================
uint8_t bitalinoMac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}; // 使用するセンサのMACアドレス
const uint8_t ECG_CHANNEL = 0;  // 0 = A1, 1 = A2
const char* BT_PIN = "0000";  // 使用するセンサのPINキー
// ==========================================

// --- フィルタ用変数 (fs = 100Hz) ---
// 1. ハイパスフィルタ (カットオフ約0.84Hz: ベースライン除去)
float hpPrevIn = 512.0f;
float hpPrevOut = 0.0f;

// 2. 2次バターワース・ローパスフィルタ (カットオフ18Hz: 40-50Hz電源ノイズ強力除去)
const float LP_B0 = 0.17509f, LP_B1 = 0.35017f, LP_B2 = 0.17509f;
const float LP_A1 = -0.51931f, LP_A2 = 0.21965f;
float lpX1 = 0.0f, lpX2 = 0.0f;
float lpY1 = 0.0f, lpY2 = 0.0f;

// 3. Pan-Tompkins風 R波検出用 (微分 -> 2乗 -> 80ms移動積分)
float prevEcg = 0.0f;
const int INT_WIN = 8;  // 8サンプル = 80ms
float sqBuf[INT_WIN] = {0};
int sqIdx = 0;
float sqSum = 0.0f;

float peakEnergy = 100.0f;
unsigned long lastRPeakMs = 0;

// BPM算出用（直近5拍の中央値フィルタ）
const int RR_N = 5;
float rrBuffer[RR_N] = {850, 850, 850, 850, 850};
int rrIdx = 0;
int validBeatCount = 0;
float currentBPM = 0.0f;
bool invertDisplay = false;
bool isConnected = false;

// ライブ波形・検出マーク描画用バッファ
const int WAVE_W = 320;
const int WAVE_H = 100;
const int WAVE_Y = 115;
int16_t waveBuf[WAVE_W] = {0};
bool peakMarkBuf[WAVE_W] = {false};
int waveHead = 0;
unsigned long lastDrawMs = 0;

void resetDetector() {
  peakEnergy = 100.0f;
  validBeatCount = 0;
  currentBPM = 0.0f;
  sqSum = 0.0f;
  for (int i = 0; i < INT_WIN; i++) sqBuf[i] = 0.0f;
}

uint8_t calcCRC4(const uint8_t* data, int len) {
  uint8_t crc = 0;
  for (int i = 0; i < len; i++) {
    uint8_t b = (i == len - 1) ? (data[i] & 0xF0) : data[i];
    for (int bit = 7; bit >= 0; bit--) {
      crc <<= 1;
      if (crc & 0x10) crc ^= 0x03;
      if (b & (1 << bit)) crc ^= 0x01;
    }
  }
  return crc & 0x0F;
}

bool connectHeartBIT() {
  canvas.fillSprite(BLACK);
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(WHITE);
  canvas.setTextSize(2);
  canvas.drawString("Connecting HeartBIT...", 160, 110);
  canvas.pushSprite(0, 0);

  SerialBT.disconnect();
  delay(200);

  if (SerialBT.connect(bitalinoMac)) {
    delay(300);
    SerialBT.write(0x83);  // 100Hz
    delay(50);
    uint8_t startCmd = 0x01 | (1 << (2 + ECG_CHANNEL));
    SerialBT.write(startCmd);
    resetDetector();
    return true;
  }

  canvas.fillSprite(BLACK);
  canvas.setTextColor(RED);
  canvas.drawString("Connect Failed!", 160, 100);
  canvas.setTextColor(LIGHTGREY);
  canvas.drawString("Press BtnC to Retry", 160, 135);
  canvas.pushSprite(0, 0);
  return false;
}

void renderScreen() {
  canvas.fillSprite(BLACK);

  // ヘッダー
  canvas.setTextDatum(TL_DATUM);
  canvas.setTextSize(1.5);
  canvas.setTextColor(LIGHTGREY);
  canvas.drawString("HeartBIT Filtered", 10, 8);

  // BPM数値
  bool beatFlash = (millis() - lastRPeakMs) < 150;
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextSize(5);
  canvas.setTextColor(beatFlash ? RED : WHITE);
  if (validBeatCount >= 3 && currentBPM > 35.0f && currentBPM < 200.0f) {
    canvas.drawString(String((int)round(currentBPM)), 135, 65);
  } else {
    canvas.drawString("---", 135, 65);
  }

  canvas.setTextDatum(ML_DATUM);
  canvas.setTextSize(2.5);
  canvas.setTextColor(CYAN);
  canvas.drawString("BPM", 210, 72);

  // 波形エリア
  canvas.drawFastHLine(0, WAVE_Y, WAVE_W, DARKGREY);
  canvas.drawFastHLine(0, WAVE_Y + WAVE_H, WAVE_W, DARKGREY);

  int centerY = WAVE_Y + WAVE_H / 2;
  for (int x = 1; x < WAVE_W; x++) {
    int idxPrev = (waveHead + x - 1) % WAVE_W;
    int idxCurr = (waveHead + x) % WAVE_W;
    int y1 = constrain(centerY - waveBuf[idxPrev], WAVE_Y + 2, WAVE_Y + WAVE_H - 2);
    int y2 = constrain(centerY - waveBuf[idxCurr], WAVE_Y + 2, WAVE_Y + WAVE_H - 2);
    canvas.drawLine(x - 1, y1, x, y2, GREEN);

    // R波検出位置に赤いマーカーを表示
    if (peakMarkBuf[idxCurr]) {
      canvas.fillCircle(x, y2, 3, RED);
    }
  }

  // 下部ガイド
  canvas.setTextDatum(BC_DATUM);
  canvas.setTextSize(1.5);
  canvas.setTextColor(LIGHTGREY);
  canvas.drawString("[Flip View]", 55, 236);
  canvas.drawString("[Reset]", 160, 236);
  canvas.drawString("[Reconnect]", 265, 236);

  canvas.pushSprite(0, 0);
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  canvas.setPsram(true);
  canvas.createSprite(320, 240);

  SerialBT.begin("M5Core2_HeartBIT", true);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  SerialBT.setPin(BT_PIN, 4);
#else
  SerialBT.setPin(BT_PIN);
#endif

  isConnected = connectHeartBIT();
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) invertDisplay = !invertDisplay;  // 表示の上下反転のみ
  if (M5.BtnB.wasPressed()) resetDetector();                 // しきい値リセット
  if (M5.BtnC.wasPressed()) isConnected = connectHeartBIT(); // 再接続

  if (!isConnected) {
    delay(20);
    return;
  }

  static uint8_t buf[3];
  static int bufPos = 0;

  while (SerialBT.available()) {
    buf[bufPos++] = SerialBT.read();

    if (bufPos == 3) {
      uint8_t expectedCrc = buf[2] & 0x0F;
      if (calcCRC4(buf, 3) == expectedCrc) {
        int adc = ((buf[1] & 0x0F) << 6) | (buf[0] >> 2);

        // 1. ハイパスフィルタ (ベースライン除去)
        float hp = 0.95f * (hpPrevOut + (float)adc - hpPrevIn);
        hpPrevIn = (float)adc;
        hpPrevOut = hp;

        // 2. 2次バターワース・ローパスフィルタ (18Hz)
        float ecg = LP_B0 * hp + LP_B1 * lpX1 + LP_B2 * lpX2
                  - LP_A1 * lpY1 - LP_A2 * lpY2;
        lpX2 = lpX1; lpX1 = hp;
        lpY2 = lpY1; lpY1 = ecg;

        // 3. 微分 -> 2乗 -> 移動積分（極性に依存しないR波エネルギー抽出）
        float diff = ecg - prevEcg;
        prevEcg = ecg;
        float sq = diff * diff;

        sqSum -= sqBuf[sqIdx];
        sqBuf[sqIdx] = sq;
        sqSum += sq;
        sqIdx = (sqIdx + 1) % INT_WIN;
        float energy = sqSum / INT_WIN;

        // エネルギーピークへの緩やかな追従（1秒で約0.9倍までしか下がらない）
        if (energy > peakEnergy) {
          // 突発的な巨大ノイズでしきい値が飛びすぎないよう上限ガード
          float capped = min(energy, peakEnergy * 4.0f);
          peakEnergy = 0.85f * peakEnergy + 0.15f * capped;
        } else {
          peakEnergy *= 0.999f;
        }

        float threshold = max(30.0f, peakEnergy * 0.40f);
        bool detected = false;
        unsigned long now = millis();

        // 不応期 350ms（最大約171 BPM）
        if (energy > threshold && (now - lastRPeakMs) > 350) {
          unsigned long rr = now - lastRPeakMs;
          lastRPeakMs = now;
          detected = true;

          if (rr >= 350 && rr <= 1500) {
            rrBuffer[rrIdx] = (float)rr;
            rrIdx = (rrIdx + 1) % RR_N;
            if (validBeatCount < RR_N) validBeatCount++;

            // 中央値（Median）でBPMを算出
            float sorted[RR_N];
            for (int i = 0; i < validBeatCount; i++) sorted[i] = rrBuffer[i];
            std::sort(sorted, sorted + validBeatCount);
            float medianRR = sorted[validBeatCount / 2];

            currentBPM = 60000.0f / medianRR;
            Serial.printf("BPM: %.1f (RR: %lu ms, E: %.1f, Th: %.1f)\n",
                          currentBPM, rr, energy, threshold);
          }
        }

        // 波形バッファ格納
        float dispVal = invertDisplay ? -ecg : ecg;
        waveBuf[waveHead] = (int16_t)(dispVal * 0.6f);
        peakMarkBuf[waveHead] = detected;
        waveHead = (waveHead + 1) % WAVE_W;

        bufPos = 0;
      } else {
        buf[0] = buf[1];
        buf[1] = buf[2];
        bufPos = 2;
      }
    }
  }

  if (millis() - lastDrawMs >= 50) {
    lastDrawMs = millis();
    renderScreen();
  }
}