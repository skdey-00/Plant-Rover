# Plant Rover - ESP32-CAM (Arduino IDE)

ESP32-CAM video streaming module for Plant Rover.

## Hardware

AiThinker ESP32-CAM module with OV2640 camera.

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
6. Search for **"esp32cam"**
7. Install **"AI Thinker ESP32-CAM"** by ESP32

### 2. Install ESP32Servo Library (if needed for other projects)

Go to **Tools → Manage Libraries** and install:
- **ESP32Servo** by Kevin Harrington

## Upload Instructions

### Wiring for USB-Serial Adapter

```
USB-Serial    ESP32-CAM
---------    ----------
  5V    ---->    5V
 GND   ---->    GND
 RX    ---->    U0R
 TX    ---->    U0T
 GPIO0 ---->    GND  (only during upload!)
```

### Upload Steps

1. Connect ESP32-CAM as shown above
2. In Arduino IDE:
   - **Tools → Board** → "AI Thinker ESP32-CAM"
   - **Tools → Port** → Select your COM port
3. **IMPORTANT**: Hold **GPIO0 to GND** while clicking upload
4. Release GPIO0 after upload completes
5. Press **EN** button on ESP32-CAM to reset

### First Boot

Open **Serial Monitor** (baud 115200):
```
=================================
ESP32-CAM Stream Server
=================================

Stream URLs:
==================================
MJPEG Stream:  http://192.168.4.2:81/stream
Single Frame: http://192.168.4.2:81/capture
Max clients:   4
==================================
Ready to stream!
```

## Usage

1. ESP32-CAM will automatically connect to "PlantRover" WiFi
2. Detection server fetches frames from `http://192.168.4.2:81/capture`
3. Dashboard displays stream from `http://192.168.4.2:81/stream`

## Endpoints

| Endpoint | Description |
|----------|-------------|
| `/stream` | MJPEG video stream (up to 4 clients) |
| `/capture` | Single JPEG frame for detection |

## Troubleshooting

**Upload fails:**
- Make sure GPIO0 is connected to GND
- Try pressing the **BOOT** button while uploading
- Use a good quality USB-Serial adapter

**No image/white screen:**
- Check camera module is properly connected
- Try reducing `xclk_freq_hz` to 10000000 in code
- Power cycle the ESP32-CAM

**WiFi won't connect:**
- Make sure "PlantRover" AP is active (upload ESP32 WROOM first)
- Move ESP32-CAM closer to the ESP32 WROOM
- Check antenna is connected

**Image is poor quality:**
- Adjust `jpeg_quality` (lower = better quality, 10-15 is good range)
- Adjust camera sensor settings in `setup()`
