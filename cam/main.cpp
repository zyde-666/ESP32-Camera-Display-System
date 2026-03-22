#include "esp_camera.h"
#include "SD_MMC.h"

// ============ AI Thinker ESP32-CAM Pins ============
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5

#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ============ Protocol ============
static const uint8_t FRAME_HEAD[2] = {0xAA, 0x55};  // frame start
static const uint8_t LINE_HEAD[2]  = {0x55, 0xAA};  // line start

static const uint8_t CMD_CAPTURE = 0xA5;
static const uint8_t CMD_START   = 0xA6;
static const uint8_t CMD_STOP    = 0xA7;

// 3-byte ACK packet: F0 0F 5A  (low collision probability vs image data)
static const uint8_t ACK0 = 0xF0;
static const uint8_t ACK1 = 0x0F;
static const uint8_t ACK2 = 0x5A;

// ============ State ============
static volatile bool streaming = false;
static volatile bool capturing = false;

// ============ Helpers ============
static void fillPins(camera_config_t &config) {
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;

  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
}

static void initCameraPreview() {
  camera_config_t config = {};
  fillPins(config);

  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size   = FRAMESIZE_QQVGA; // 160x120
  config.fb_count     = 1;

  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.grab_mode    = CAMERA_GRAB_LATEST;

  if (esp_camera_init(&config) != ESP_OK) {
    // 避免输出到同一串口污染协议，这里不打印，直接卡死
    while (true) delay(1000);
  }

  sensor_t *s = esp_camera_sensor_get();
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
}

static void initCameraPhoto() {
  camera_config_t config = {};
  fillPins(config);

  config.pixel_format = PIXFORMAT_JPEG;

  // 先用 VGA 更稳，SVGA 稳定后再换
  //config.frame_size   = FRAMESIZE_VGA;      // 640x480
  config.frame_size = FRAMESIZE_SVGA;    // 800x600（稳了再开）

  config.jpeg_quality = 10;                // 10~20
  config.fb_count     = 2;

  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.grab_mode    = CAMERA_GRAB_LATEST;

  if (esp_camera_init(&config) != ESP_OK) {
    while (true) delay(1000);
  }

  sensor_t *s = esp_camera_sensor_get();
  s->set_vflip(s, 1);     // 上下翻转
  s->set_hmirror(s, 1);   // 左右镜像
}

static void sendAck() {
  Serial.write(ACK0);
  Serial.write(ACK1);
  Serial.write(ACK2);
}

static void takePhotoToSD() {
  // 停止预览模式（非常关键）
  esp_camera_deinit();
  delay(50);

  // 进入拍照模式
  initCameraPhoto();
  delay(200);

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    esp_camera_deinit();
    initCameraPreview();
    sendAck(); // 即使失败也 ACK，避免对端死等
    return;
  }

  char path[32];
  sprintf(path, "/IMG_%lu.jpg", millis());

  File file = SD_MMC.open(path, FILE_WRITE);
  if (file) {
    file.write(fb->buf, fb->len);
    file.flush();
    file.close();
  }
  esp_camera_fb_return(fb);

  // 回预览
  esp_camera_deinit();
  delay(50);
  initCameraPreview();
  delay(100);

  // 丢弃几帧稳定
  for (int i = 0; i < 3; i++) {
    camera_fb_t *tmp = esp_camera_fb_get();
    if (tmp) esp_camera_fb_return(tmp);
  }

  sendAck();
}

// ============ Setup / Loop ============
void setup() {

  // 230400 与对端一致
  Serial.begin(230400);
  delay(300);

  // 摄像头预览 init
  initCameraPreview();

  // SD：强制 1-bit 模式更稳 + 降频
  bool sd_ok = SD_MMC.begin("/sdcard", true);  // true=1-bit

  // 如需串口调试可打印，但用 UART0 做协议，建议先别打印到 Serial
  (void)sd_ok;

  // 默认不 streaming，等待对端发 START
  streaming = false;
}

void loop() {
  // ----------- Read Commands -----------
  while (Serial.available()) {
    uint8_t c = (uint8_t)Serial.read();

    if (c == CMD_START) {
      streaming = true;
      //digitalWrite(4, HIGH);
    } 
    else if (c == CMD_STOP) {
      streaming = false;
      //digitalWrite(4, LOW);
    } 
    else if (c == CMD_CAPTURE) {
      capturing = true;     // 关键：触发拍照
    }
  }
  // ----------- Capture -----------
  if (capturing) {
    capturing = false;
    streaming = false;      // 拍照时强制停止预览输出
    takePhotoToSD();
    return;
  }

  // ----------- Preview Stream -----------
  if (!streaming) {
    delay(5);
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  // 帧头
  Serial.write(FRAME_HEAD, 2);

  // 每次发一行（原来是隔行发+对端画两行，这里保持一致）
  for (int y = 0; y < fb->height; y += 2) {
    Serial.write(LINE_HEAD, 2);
    Serial.write(fb->buf + y * fb->width * 2, fb->width * 2);
  }

  esp_camera_fb_return(fb);
}
