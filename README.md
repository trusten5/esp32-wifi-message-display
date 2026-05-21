# ESP32 WiFi Message Display

A WiFi-connected physical message display built on an ESP32. An LED ring lights up when a new message arrives over MQTT — tapping a paired NFC card or fob to the RFID reader reveals it on a 128×64 OLED display.

Also cycles through ambient info screens: dual-timezone clock, live weather display, distance tracker, and a pixel-art screensaver.

---

## Hardware

- ESP32 (tested on ESP32-S3)
- MFRC522 RFID reader
- SSD1306 128×64 OLED (I2C)
- NeoPixel LED ring
- NFC card + fob (any 13.56 MHz Mifare)

---

## Setup

**1. Copy config**
```bash
cp config.h.example config.h
```

**2. Fill in `config.h`** with your:
- WiFi credentials
- Adafruit IO username, key, and feed name
- OpenWeatherMap API key and city ID
- RFID UIDs (run a UID scanner sketch to find yours)
- Timezone strings and clock labels
- Coordinates for the distance screen (optional)

**3. Install libraries** via Arduino Library Manager:
- `Adafruit MQTT Library`
- `Adafruit SSD1306`
- `Adafruit GFX Library`
- `MFRC522`
- `ArduinoJson`

**4. Upload** `mbox.ino` to your ESP32.

---

## Animation Codes

Send these strings over MQTT to trigger OLED animations instead of plain text:

| Code | Animation |
|---|---|
| `__HEART__` | Pulsing heart |
| `__SUNRISE__` | Rising sun |
| `__MOON__` | Crescent moon with stars |
| `__FLOWER__` | Blooming flower |
| `__FIREWORKS__` | Fireworks burst |
| `__STARS__` | Twinkling stars |

Any other string displays as plain centered text.

---

## Sending Messages

Use the [Adafruit IO](https://io.adafruit.com) dashboard, their mobile app, or any MQTT client to publish to your feed. The device picks it up instantly and lights the LED ring until the message is revealed by an NFC tap.
