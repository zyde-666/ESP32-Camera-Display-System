# ESP32 Camera Display System

## 📷 Overview

This project implements a **real-time camera preview and capture system** using:

* **ESP32-CAM** → image capture
* **ESP32** → data processing & control
* **TFT display (ST7735)** → real-time preview

The system supports:

* Live video streaming (RGB565)
* Button-triggered photo capture
* Saving images to SD card (JPEG)

---

## 🚀 Features

* 🎥 **Real-time Preview**

  * QQVGA (160×120) RGB565 stream
  * Line-based transmission for stability

* 📸 **Photo Capture**

  * Button-triggered snapshot
  * High-resolution JPEG (up to SVGA)
  * Saved to SD card

* 🔌 **Custom UART Protocol**

  * Frame-based transmission
  * Line synchronization
  * ACK mechanism to avoid data corruption

* 🧠 **Robust Streaming System**

  * State machine parser
  * Timeout recovery
  * Frame resynchronization

---

## 🧱 System Architecture

```
ESP32-CAM
   │  (UART - custom protocol)
   ▼
ESP32 (controller)
   │  (SPI)
   ▼
TFT Display
```

---

## 🔄 Communication Protocol

### Frame Structure

```
Frame Start:  AA 55
Line Start:   55 AA
Data:         RGB565 (per line)
```

### Commands

| Command | Value | Description     |
| ------- | ----- | --------------- |
| START   | 0xA6  | Start streaming |
| STOP    | 0xA7  | Stop streaming  |
| CAPTURE | 0xA5  | Take photo      |

### ACK Packet

```
F0 0F 5A
```

Used to confirm successful photo capture.

---

## 🖥️ Display (TFT)

* Driver: **ST7735**
* Resolution: 160×128
* Interface: SPI
* Real-time rendering using `drawRGBBitmap`

---

## 🔘 User Interaction

* Button (GPIO25):

  * Press → capture image
  * System:

    1. Stop stream
    2. Capture JPEG
    3. Save to SD
    4. Resume preview

---

## 🔌 Wiring

### ESP32 ↔ TFT

```
18 → SCL
23 → SDA
5  → CS
16 → DC
17 → RST
3.3V → VCC
GND → GND
```

### ESP32 ↔ Button

```
GPIO25 → Button → GND
```

### ESP32 ↔ ESP32-CAM

```
GPIO33 (TX) → CAM RX (GPIO3)
GPIO32 (RX) ← CAM TX (GPIO1)
GND ↔ GND
```

---

## ⚙️ Configuration

### Camera Modes

| Mode    | Format | Resolution     |
| ------- | ------ | -------------- |
| Preview | RGB565 | 160×120        |
| Photo   | JPEG   | 800×600 (SVGA) |

---

## 🧪 Stability Design

* Line-based streaming (instead of full frame)
* UART buffer flushing before capture
* Frame re-sync state machine
* Timeout recovery for corrupted data
* Drop initial frames after mode switch

---

## 📂 Project Structure

```
camera/      # ESP32-CAM code
display/     # ESP32 + TFT code
docs/        # wiring, notes
```

---

## 📸 Demo

> (Add your photo here)

Example:

* TFT preview output
* Hardware setup

---

## 🧠 What I Learned

* Designing a **custom binary protocol**
* Handling **real-time data streams over UART**
* Synchronization & error recovery
* Embedded multi-device system integration
* Performance tuning under bandwidth constraints

---

## 📌 Notes

* UART baud rate: **230400**
* SD card runs in **1-bit mode** for stability
* PSRAM is required for camera frame buffer

---

## ⭐ Future Improvements

* Higher resolution streaming
* Compression (JPEG stream)
* WiFi streaming
* Touch UI / controls

---

## 📄 License

MIT License
