# 🌱 Plant Rover

An autonomous plant inspection rover equipped with computer vision, ML-based disease detection, and automated pesticide spraying system.

## 🎯 Features

- **WiFi Remote Control** - Control the rover via web dashboard from your phone/computer
- **Live Camera Feed** - Real-time video streaming from ESP32-CAM
- **AI Disease Detection** - YOLOv8-based detection of fungus and pest infestations
- **Auto-Spray System** - Automatic pesticide spraying when threats are detected
- **Differential Drive** - Smooth joystick-controlled movement with L298N motor driver
- **Safety Systems** - Watchdog timer, PC offline detection, spray rate limiting

---

## 📦 Hardware Requirements

### Main Components

| Component | Quantity | Purpose |
|-----------|----------|---------|
| ESP32 WROOM-32 | 1 | Main controller |
| ESP32-CAM (AiThinker) | 1 | Camera module |
| BO Motors | 4 | Wheel drive |
| L298N Motor Driver | 1 | Motor control |
| SG90 Servos | 3 | Spray nozzle control |
| Li-Ion Battery (18650 x3) | 1 | Power supply |
| Chassis with wheels | 1 | Rover body |

### Additional Parts
- Jumper wires (male-to-female, male-to-male)
- Breadboard or PCB
- Spray bottles (2x)
- 5V 2A power supply for testing

---

## 🔌 Wiring Guide

### ESP32 WROOM-32 Pin Connections

```
┌─────────────────────────────────────────────────────────────────┐
│                    ESP32 WROOM-32                               │
├─────────────────────────────────────────────────────────────────┤
│  MOTOR A (LEFT)          MOTOR B (RIGHT)                        │
│  IN1  → GPIO 22          IN3  → GPIO 27                        │
│  IN2  → GPIO 26          IN4  → GPIO 14                        │
│  ENA  → GPIO 32 (PWM)    ENB  → GPIO 33 (PWM)                  │
├─────────────────────────────────────────────────────────────────┤
│  SERVOS                                                           │
│  Servo 1 (Camera/Gimbal) → GPIO 18                              │
│  Servo 2 (Spray Left)    → GPIO 19                              │
│  Servo 3 (Spray Right)   → GPIO 21                              │
├─────────────────────────────────────────────────────────────────┤
│  POWER                                                            │
│  VIN  → Battery (+) 5V-12V                                       │
│  GND  → Battery (-)                                               │
└─────────────────────────────────────────────────────────────────┘
```

### L298N Motor Driver Connections

```
         L298N Motor Driver
┌────────────────────────────────────┐
│  OUT1 ──┐                          │
│  OUT2 ──┼── LEFT MOTOR (Motors 1&2) │
│         │                          │
│  OUT3 ──┐                          │
│  OUT4 ──┼── RIGHT MOTOR (Motors 3&4)│
│                                 │
│  IN1  ← ESP32 GPIO 22            │
│  IN2  ← ESP32 GPIO 26            │
│  ENA  ← ESP32 GPIO 32            │
│                                 │
│  IN3  ← ESP32 GPIO 27            │
│  IN4  ← ESP32 GPIO 14            │
│  ENB  ← ESP32 GPIO 33            │
│                                 │
│  12V  ── Battery (+)             │
│  GND  ── Battery (-)             │
│  5V   ── Can power ESP32          │
└────────────────────────────────────┘
```

### ESP32-CAM (AiThinker) Pin Configuration

```
The ESP32-CAM uses the standard AiThinker pin mapping.
No external wiring needed for camera module.

Power:
- 5V  → Battery (+) or 5V supply
- GND → Battery (-)

Connection:
- Connects to "PlantRover" WiFi AP hosted by ESP32 WROOM
```

---

## 🚀 Setup Instructions

### Quick Setup (Windows)

**Double-click `setup.bat`** - This automated script will:
- ✅ Download and install Python 3.11 (if not installed)
- ✅ Install all ML training libraries
- ✅ Install detection server dependencies
- ✅ Install labelImg for dataset annotation

Just run it and wait for completion! (~5-10 minutes)

---

### Manual Setup

### 1. Install Arduino IDE

1. Download Arduino IDE from [arduino.cc](https://www.arduino.cc/en/software)
2. Open Arduino IDE
3. Go to **File → Preferences**
4. Add this URL to "Additional Boards Manager":
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
5. Go to **Tools → Board → Boards Manager**
6. Search for "ESP32" and install **ESP32 by Espressif Systems**

### 2. Install Required Libraries

In Arduino IDE, go to **Sketch → Include Library → Manage Libraries** and install:

- **WebSockets** by Markus Sattler (Links2004)
- **ESP32Servo** by Kevin Harrington
- **ArduinoJson** by Benoit Blanchon

### 3. Upload ESP32 WROOM Firmware

1. Open `plant_rover_arduino/plant_rover/plant_rover.ino`
2. Select Board: **ESP32 Dev Module**
3. Select correct COM port
4. Click **Upload**

### 4. Upload ESP32-CAM Firmware

**Important:** ESP32-CAM requires a special upload procedure:

1. Connect GPIO0 to GND
2. Open `plant_cam_arduino/plant_cam/plant_cam.ino`
3. Select Board: **AI Thinker ESP32-CAM**
4. Select correct COM port
5. Click **Upload**
6. After upload, disconnect GPIO0 from GND
7. Press RESET button

### 5. Power On Sequence

1. First: Power on ESP32 WROOM
2. Wait for "Plant Rover Ready!" in Serial Monitor
3. Second: Power on ESP32-CAM
4. ESP32-CAM will auto-connect to PlantRover WiFi

---

## 📱 Using the Dashboard

### Connect Your Device

1. Open WiFi settings on your phone/computer
2. Connect to: **`PlantRover`**
3. Password: **`rover1234`**
4. Open browser and go to: **`http://192.168.4.1`**

### Dashboard Controls

| Control | Function |
|---------|----------|
| **Joystick** | Drag to drive - direction and speed |
| **D-Pad** | Quick directional control (F/B/L/R) |
| **Home Button** | Reset servo to 90° (forward-facing) |
| **Spray Left** | Trigger left spray nozzle |
| **Spray Right** | Trigger right spray nozzle |
| **Spray Both** | Trigger both nozzles simultaneously |
| **Toggle Auto** | Enable/disable automatic spraying |

### Serial Monitor Testing

You can also test motors directly from Arduino Serial Monitor:

1. Open Serial Monitor (baud rate: **115200**)
2. Set "Both NL & CR" at the bottom
3. Send commands:

| Command | Action |
|---------|--------|
| `F` or `W` | Forward |
| `B` or `S` | Backward |
| `L` or `A` | Left (pivot turn) |
| `R` or `D` | Right (pivot turn) |
| `X` or Space | Stop motors |
| `H` or `?` | Show help |

---

## 🤖 ML Training (Optional)

> **Note:** If you ran `setup.bat` on Windows, Python and all libraries are already installed!

For training your own disease detection model:

### Requirements (Linux/Mac or if not using setup.bat)

```bash
cd plant_rover_training
pip install -r requirements.txt
```

### Quick Start

1. **Setup labeling environment:**
   ```bash
   python setup_labeling.py
   ```

2. **Label your images** using labelImg (launched automatically)

3. **Prepare dataset:**
   ```bash
   python prepare_dataset.py
   ```

4. **Train model:**
   ```bash
   python train_model.py
   ```

5. **Verify model:**
   ```bash
   python verify_model.py path/to/test/images
   ```

---

## 🖥️ Detection Server (Optional)

> **Note:** If you ran `setup.bat` on Windows, all dependencies are already installed!

Run the detection server on your PC for automated spraying:

**Windows (after setup.bat):**
```bash
cd detection_server
python detection_server.py
```

**Linux/Mac or manual setup:**
```bash
cd detection_server
pip install -r requirements.txt
python detection_server.py
```

The server will:
- Poll ESP32-CAM for images every 500ms
- Run YOLOv8 inference
- Automatically trigger sprays when fungus/pest detected (confidence > 70%)
- Serve camera stream at `http://localhost:5000/stream`

---

## 📂 Project Structure

```
Plant-Rover/
├── setup.bat                     # Automated setup for Windows (double-click me!)
├── plant_rover_arduino/          # ESP32 WROOM firmware (Arduino IDE)
│   └── plant_rover/
│       └── plant_rover.ino       # Main controller code
├── plant_cam_arduino/            # ESP32-CAM firmware
│   └── plant_cam/
│       └── plant_cam.ino         # Camera streaming code
├── plant_rover_training/         # ML training pipeline
│   ├── setup_labeling.py
│   ├── prepare_dataset.py
│   ├── augment_dataset.py
│   ├── train_model.py
│   └── verify_model.py
├── detection_server/             # PC detection server
│   ├── detection_server.py
│   ├── config.py
│   └── requirements.txt
└── README.md                     # This file
```

---

## ⚙️ Configuration

### WiFi Settings

Edit in `plant_rover.ino`:
```cpp
const char* AP_SSID = "PlantRover";      // WiFi name
const char* AP_PASSWORD = "rover1234";  // WiFi password
```

### Motor Pins

Edit in `plant_rover.ino`:
```cpp
// Motor A (Left)
const int MOTOR_A_IN1 = 25;
const int MOTOR_A_IN2 = 26;
const int MOTOR_A_ENA = 32;

// Motor B (Right)
const int MOTOR_B_IN3 = 27;
const int MOTOR_B_IN4 = 14;
const int MOTOR_B_ENB = 33;
```

### Servo Pins

Edit in `plant_rover.ino`:
```cpp
const int SERVO1_PIN = 18;  // Gimbal/Camera
const int SERVO2_PIN = 19;  // Spray Left
const int SERVO3_PIN = 21;  // Spray Right
```

---

## 🔧 Troubleshooting

### ESP32-CAM won't connect to WiFi
- Ensure ESP32 WROOM is powered on first
- Check that both devices use the same WiFi credentials
- Move ESP32-CAM closer to ESP32 WROOM

### Camera feed shows "Offline"
- Check ESP32-CAM is powered on
- Verify ESP32-CAM connected to PlantRover WiFi
- Try accessing `http://192.168.4.2:81/stream` directly

### Motors not working
- Check L298N power connections (12V input)
- Verify all 6 motor control wires are connected
- Test motors by connecting directly to 5V

### Dashboard won't load
- Clear browser cache
- Check you're connected to PlantRover WiFi
- Verify ESP32 WROOM is running (check Serial Monitor)

---

## 📊 Specifications

| Specification | Value |
|---------------|-------|
| WiFi Network | 802.11 b/g/n AP mode |
| WiFi Range | ~20 meters (open space) |
| Motor Speed | 0-255 PWM per channel |
| Servo Range | 0-180 degrees |
| Camera Resolution | 800x600 (SVGA) |
| Detection Threshold | 70% confidence |
| Spray Cooldown | 5 seconds |
| Watchdog Timeout | 3 seconds (motor stop) |
| PC Offline Timeout | 10 seconds |

---

## 📝 API Reference

### WebSocket Commands (Port 81)

```javascript
// Drive with joystick (x: -255 to 255, y: -255 to 255)
{"type":"drive","x":-100,"y":200}

// Control Servo 1
{"type":"servo1","angle":90}

// Trigger spray
{"type":"spray","id":2}           // Left nozzle
{"type":"spray","id":3}           // Right nozzle
{"type":"spray","id":"both"}      // Both nozzles

// Toggle auto-spray
{"type":"autoSpray","enabled":true}
```

### HTTP Endpoints (Port 80)

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Dashboard HTML |
| `/status` | GET | JSON status |
| `/detection` | POST | Receive detection from PC |

---

## 📄 License

This project is open source and available for educational and personal use.

---

## 🤝 Contributing

Contributions are welcome! Please feel free to submit issues or pull requests.

---

**Built with ❤️ for autonomous plant care**
