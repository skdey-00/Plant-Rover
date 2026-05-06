# ESP32-CAM MJPEG Stream Server

Minimal ESP32-CAM sketch for streaming MJPEG video over WiFi.

## Hardware

- **Board**: AiThinker ESP32-CAM
- **Connector**: USB-Serial adapter (5V, GND, TX, RX)

## Wiring (USB-Serial to ESP32-CAM)

```
USB-Serial      ESP32-CAM
---------      ----------
    5V    ---->    5V
   GND    ---->    GND
    TX    ---->    U0R (GPIO3)
    RX    ---->    U0T (GPIO1)
```

**Important**: Keep GPIO0 connected to GND during upload, then disconnect for normal operation.

## Features

| Feature | Description |
|---------|-------------|
| WiFi | Connects to "PlantRover" AP |
| Stream | MJPEG on port 81 at `/stream` |
| Capture | Single JPEG at `/capture` |
| Resolution | SVGA (800x600) |
| Quality | JPEG quality 12 |
| Frame Rate | ~20 FPS |

## Building & Uploading

```bash
cd plant_rover_esp32cam
pio run --target upload
pio device monitor
```

## Endpoints

### MJPEG Stream
```
http://<ESP32_IP>:81/stream
```
Returns continuous MJPEG stream for browser viewing or processing.

### Single Frame
```
http://<ESP32_IP>:81/capture
```
Returns a single JPEG frame (useful for ML detection pipeline).

## Example: Capture with Python

```python
import requests

url = "http://192.168.4.2:81/capture"
response = requests.get(url)

with open("capture.jpg", "wb") as f:
    f.write(response.content)
```

## Example: Stream with OpenCV

```python
import cv2

url = "http://192.168.4.2:81/stream"
cap = cv2.VideoCapture(url)

while True:
    ret, frame = cap.read()
    if ret:
        cv2.imshow("ESP32-CAM", frame)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

cap.release()
cv2.destroyAllWindows()
```

## Serial Output on Boot

```
=================================
📷 ESP32-CAM Stream Server
=================================

Initializing camera...
Camera initialized successfully
Connecting to WiFi: PlantRover
....
WiFi connected!
IP Address: 192.168.4.2
Stream server started on port 81

=================================
📷 Stream URLs:
=================================
MJPEG Stream:  http://192.168.4.2:81/stream
Single Frame: http://192.168.4.2:81/capture
=================================

Ready to stream!
```

## Troubleshooting

**Camera init failed**:
- Check camera module connection
- Try reducing XCLK frequency to 10MHz
- Ensure adequate power supply (5V, 1A+ recommended)

**WiFi connection failed**:
- Verify "PlantRover" AP is active
- Check password: "rover1234"
- Move ESP32-CAM closer to AP

**No stream**:
- Try different browser (Chrome/Firefox)
- Check firewall settings
- Verify correct IP address
