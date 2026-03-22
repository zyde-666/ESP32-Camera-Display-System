#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

// ============ UART Protocol ============
static const uint8_t FRAME_AA = 0xAA;
static const uint8_t FRAME_55 = 0x55;
static const uint8_t LINE_55  = 0x55;
static const uint8_t LINE_AA  = 0xAA;

static const uint8_t CMD_CAPTURE = 0xA5;
static const uint8_t CMD_START   = 0xA6;
static const uint8_t CMD_STOP    = 0xA7;

// ACK packet from CAM: F0 0F 5A
static const uint8_t ACK0 = 0xF0;
static const uint8_t ACK1 = 0x0F;
static const uint8_t ACK2 = 0x5A;

// ============ TFT ============
#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  17
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// ============ SPI pins (as you used) ============
#define TFT_SCK  18
#define TFT_MOSI 23

// ============ UART pins ============
#define CAM_RX 32  // ESP32 RX <- CAM TX0(GPIO1)
#define CAM_TX 33  // ESP32 TX -> CAM RX0(GPIO3)

// ============ UI / Preview ============
#define PREVIEW_W 160
#define PREVIEW_H 120
#define UI_Y 120
#define UI_H 40

// ============ Button ============
#define BTN_SHUTTER 25

// ============ Buffers ============
static uint8_t  byteBuf[PREVIEW_W * 2];  // 320 bytes
static uint16_t lineBuf[PREVIEW_W];
static int recvLen = 0;
static int draw_y = 0;

// ============ RX state ============
enum RxState {
  WAIT_FRAME_AA,
  WAIT_FRAME_55,
  WAIT_LINE_55,
  WAIT_LINE_AA,
  READ_LINE_BYTES
};
static RxState state = WAIT_FRAME_AA;

// ACK detect state
static int ackState = 0;
static volatile bool gotAck = false;

// Timeout
static unsigned long lastByteMs = 0;

// FPS
static unsigned long lastFpsTime = 0;
static int frameCount = 0;
static int fps = 0;

// Button debounce
static bool lastBtn = HIGH;
static unsigned long lastShotMs = 0;

static bool waitingAck = false;

static void uiStatus(const char *msg) {
  tft.fillRect(0, UI_Y, PREVIEW_W, UI_H, ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(1);
  tft.setCursor(2, UI_Y + 2);
  tft.print(msg);

  tft.setCursor(2, UI_Y + 14);
  tft.print("FPS:");
  tft.print(fps);
}

static void resetRx() {
  state = WAIT_FRAME_AA;
  recvLen = 0;
  draw_y = 0;
  ackState = 0;
  gotAck = false;
}

static void sendCmd(uint8_t c) {
  Serial2.write(c);
}

static void flushUart(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    while (Serial2.available()) (void)Serial2.read();
    delay(1);
  }
}

static void parseByte(uint8_t b) {
  lastByteMs = millis();

  // ---- ACK detector ----
  if (!gotAck) {
    if (ackState == 0 && b == ACK0) ackState = 1;
    else if (ackState == 1 && b == ACK1) ackState = 2;
    else if (ackState == 2 && b == ACK2) {
      gotAck = true;
      ackState = 0;
      waitingAck = false;
      return;   // ⭐ 非常关键：ACK 字节不再进入视频解析
    } else ackState = 0;
  }

  // ⭐ 如果正在等 ACK，直接忽略其余字节
  if (waitingAck) return;

  switch (state) {
    case WAIT_FRAME_AA:
      if (b == FRAME_AA) state = WAIT_FRAME_55;
      break;

    case WAIT_FRAME_55:
      if (b == FRAME_55) {
        draw_y = 0;
        state = WAIT_LINE_55;
      } else {
        state = WAIT_FRAME_AA;
      }
      break;

    case WAIT_LINE_55:
      if (b == LINE_55) state = WAIT_LINE_AA;
      else if (b == FRAME_AA) state = WAIT_FRAME_55; // allow quick relock
      break;

    case WAIT_LINE_AA:
      if (b == LINE_AA) {
        recvLen = 0;
        state = READ_LINE_BYTES;
      } else {
        state = WAIT_LINE_55;
      }
      break;

    case READ_LINE_BYTES:
      byteBuf[recvLen++] = b;
      if (recvLen >= PREVIEW_W * 2) {
        for (int i = 0; i < PREVIEW_W; i++) {
          lineBuf[i] = (byteBuf[i * 2] << 8) | byteBuf[i * 2 + 1];
        }

        // 画两行（对应 CAM 端 y += 2）
        tft.drawRGBBitmap(0, draw_y,     lineBuf, PREVIEW_W, 1);
        tft.drawRGBBitmap(0, draw_y + 1, lineBuf, PREVIEW_W, 1);

        draw_y += 2;
        if (draw_y >= PREVIEW_H) {
          draw_y = 0;
          frameCount++;
        }
        state = WAIT_LINE_55;
      }
      break;
  }
}

static void updateFps() {
  unsigned long now = millis();
  if (now - lastFpsTime >= 1000) {
    fps = frameCount;
    frameCount = 0;
    lastFpsTime = now;
  }
}

static void checkButtonAndCapture() {
  bool now = digitalRead(BTN_SHUTTER);
  if (lastBtn == HIGH && now == LOW) {
    if (millis() - lastShotMs > 350) {
      lastShotMs = millis();

      // 1) stop stream to avoid half frames
      uiStatus("Saving...");
      sendCmd(CMD_STOP);
      delay(30);
      flushUart(80);
      resetRx();

      // 2) capture
      waitingAck = true;
      gotAck = false;
      sendCmd(CMD_CAPTURE);

      // 3) wait ACK (timeout 8s)
      unsigned long t0 = millis();
      while (!gotAck && (millis() - t0) < 8000) {
        while (Serial2.available()) {
          parseByte((uint8_t)Serial2.read());
        }
        // 防止卡死
        if (state == READ_LINE_BYTES && (millis() - lastByteMs) > 50) {
          resetRx();
        }
        updateFps();
        delay(1);
      }

      // 4) restart stream
      flushUart(100);
      resetRx();
      sendCmd(CMD_START);
      uiStatus(gotAck ? "Saved OK" : "Save Timeout");
      delay(200);
    }
  }
  lastBtn = now;
}

void setup() {
  Serial.begin(115200);

  pinMode(BTN_SHUTTER, INPUT_PULLUP);

  // SPI + TFT
  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  uiStatus("Booting...");

  // UART to CAM  
  Serial2.begin(230400, SERIAL_8N1, CAM_RX, CAM_TX);

  // 给 CAM 启动留时间，并清掉 boot 垃圾
  delay(500);
  flushUart(200);
  resetRx();

  // 发 START：让 CAM 之后才开始输出视频流
  sendCmd(CMD_START);

  uiStatus("Preview");
  lastFpsTime = millis();
}

void loop() {
  // Receive and draw preview
  while (Serial2.available()) {
    parseByte((uint8_t)Serial2.read());
  }

  // Timeout recovery
  if (state == READ_LINE_BYTES && (millis() - lastByteMs) > 50) {
    resetRx();
  }

  // Button
  checkButtonAndCapture();

  // FPS / UI
  updateFps();

  static unsigned long lastUiMs = 0;
  if (millis() - lastUiMs > 1000) {
    lastUiMs = millis();
    uiStatus("Preview");
  }
  
}
