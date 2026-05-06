# Plant Rover Detection Server

FastAPI server that performs YOLOv8 inference on ESP32-CAM frames and forwards detections to the rover.

## Features

| Feature | Description |
|---------|-------------|
| Frame Fetching | Polls ESP32-CAM every 500ms |
| YOLOv8 Inference | Runs `yolov8n.pt` (swappable) |
| Detection Forwarding | POSTs to rover on fungus/pest detection |
| SSE Events | Real-time updates via `/events` |
| Stream Proxy | Single-origin MJPEG at `/stream` |
| Pause/Resume | Control detection processing |

## Installation

```bash
cd detection_server
pip install -r requirements.txt
```

## Configuration

Edit `detection_server.py` to change defaults:

```python
ESP32CAM_CAPTURE_URL = "http://192.168.4.2/capture"
ESP32CAM_STREAM_URL = "http://192.168.4.2/stream"
ESP32_ROVER_DETECTION_URL = "http://192.168.4.1/detection"

FETCH_INTERVAL = 0.5  # seconds
CONFIDENCE_THRESHOLD = 0.70
TRIGGER_CLASSES = {"fungus", "pest"}
MODEL_PATH = "yolov8n.pt"  # or custom model
```

## Running

```bash
python detection_server.py
# or
uvicorn detection_server:app --host 0.0.0.0 --port 5000 --reload
```

## API Endpoints

### GET /
Server info and status

### GET /stream
Proxied MJPEG stream from ESP32-CAM

### GET /events
Server-Sent Events stream for real-time detections

### GET /detections
Latest detection results

### POST /pause
Pause detection processing

### POST /resume
Resume detection processing

### POST /detect
Manually trigger single-frame detection

### POST /model/load
Load a different YOLOv8 model
```json
{"model_path": "custom_model.pt"}
```

## SSE Event Format

```
event: detection
data: {"class": "fungus", "confidence": 0.87, "timestamp": "2025-01-15T10:30:00Z"}

: keepalive
```

## Custom Model Training

Train a custom plant disease model:

```python
from ultralytics import YOLO

# Load pretrained model
model = YOLO("yolov8n.pt")

# Train on your dataset
model.train(
    data="plant_disease.yaml",
    epochs=100,
    imgsz=640
)
```

Then update `MODEL_PATH = "runs/detect/train/weights/best.pt"`

## Example: Subscribe to SSE Events

```python
import requests

def on_detection(event):
    data = event.json()
    print(f"Detected: {data['class']} ({data['confidence']})")

with requests.get("http://localhost:5000/events", stream=True) as resp:
    for line in resp.iter_lines():
        if line.startswith(b"data: "):
            import json
            data = json.loads(line[6:])
            print(f"Detection: {data}")
```

## Example: Frontend SSE Integration

```javascript
const eventSource = new EventSource('http://localhost:5000/events');

eventSource.addEventListener('detection', (e) => {
    const detection = JSON.parse(e.data);
    console.log('Detected:', detection.class, detection.confidence);

    // Show notification
    showNotification(`${detection.class} detected (${detection.confidence})`);
});

eventSource.addEventListener('error', (e) => {
    console.error('SSE error:', e);
});
```

## Project Integration

```
┌─────────────┐      ┌──────────────┐      ┌─────────────┐
│   ESP32-CAM │ ───▶ │   This API   │ ───▶ │ ESP32 Rover │
│   :81/capture│     │   :5000      │      │   :80/detection│
└─────────────┘      └──────────────┘      └─────────────┘
                           │
                           ▼
                    ┌──────────────┐
                    │   Dashboard  │
                    │ (Browser)    │
                    └──────────────┘
```

- **Dashboard** connects to `:5000/stream` (MJPEG) and `:5000/events` (SSE)
- **Single origin** - no CORS issues
