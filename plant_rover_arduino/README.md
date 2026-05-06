# Plant Rover - ESP32 WROOM (Arduino IDE)

ESP32 WiFi-controlled robot with motor/servo control and WebSocket API.

## Hardware

| Component | GPIO |
|-----------|------|
| Servo 1 | 18 |
| Servo 2 | 19 |
| Servo 3 | 21 |
| L298N IN1 | 25 |
| L298N IN2 | 26 |
| L298N IN3 | 27 |
| L298N IN4 | 14 |
| L298N ENA | 32 (PWM Left) |
| L298N ENB | 33 (PWM Right) |

## Arduino IDE Setup

### 1. Install ESP32 Board Support

1. Open Arduino IDE
2. Go to **File → Preferences**
3. Add this to **Additional Boards Manager URLs**:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
4. Click **OK**
5. Go to **Tools → Board → Boards Manager...**
6. Search for **"esp32"**
7. Install **"ESP32 Dev Module"** by Espressif

### 2. Install Required Libraries

Go to **Tools → Manage Libraries** and install:
- **WebSockets** by Markus Sattler
- **ArduinoJson** by Benoit Blanchon
- **ESP32Servo** by Kevin Harrington

## Upload Instructions

1. Connect ESP32 WROOM via USB
2. In Arduino IDE:
   - **Tools → Board** → "ESP32 Dev Module"
   - **Tools → Port** → Select your COM port
3. Click **Upload** (→ icon)

## First Boot

After upload, open **Serial Monitor** (magnifying glass icon):
- Set baud rate to **115200**
- You should see:
```
=================================
🌱 Plant Rover Ready!
=================================

Connect to: http://192.168.4.1
WebSocket: ws://192.168.4.1:81
```

## Usage

1. Connect your device to "PlantRover" WiFi (password: `rover1234`)
2. Open `dashboard.html` in your browser
3. Enter `192.168.4.1` as the Rover IP

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Root info page |
| `/status` | GET | JSON status |
| `/detection` | POST | {"label":"fungus","confidence":0.87} |
| WebSocket | ws://192.168.4.1:81 | Real-time control |

## WebSocket Commands

```json
{"type":"drive","x":-100,"y":200}
{"type":"servo1","angle":90}
{"type":"spray","id":2}
{"type":"spray","id":3}
{"type":"spray","id":"both"}
{"type":"autoSpray","enabled":true}
```

## Troubleshooting

**Upload fails:**
- Hold **BOOT button** on ESP32 while clicking upload
- Try a different USB cable
- Install CH340 driver if needed

**WiFi doesn't appear:**
- Press **EN** button on ESP32 to reset
- Wait 10-15 seconds after upload

**Servos don't move:**
- Check GPIO wiring
- Servos need separate 5V power supply (not from ESP32)
