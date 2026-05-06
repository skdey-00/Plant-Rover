"""
Detection Server Configuration

Edit these values to customize the server behavior.
"""

# ============================================================
# Network Configuration
# ============================================================
SERVER_HOST = "0.0.0.0"
SERVER_PORT = 5000

# ESP32-CAM (Video source) — port 81 is the ESP32-CAM's stream server
ESP32CAM_CAPTURE_URL = "http://192.168.4.2:81/capture"
ESP32CAM_STREAM_URL = "http://192.168.4.2:81/stream"

# ESP32 Rover (Motor/servo control) — port 80 is the rover's HTTP server
ESP32_ROVER_DETECTION_URL = "http://192.168.4.1/detection"

# ============================================================
# Detection Configuration
# ============================================================
# Seconds between frame fetches
FETCH_INTERVAL = 0.5

# Confidence threshold for triggering spray
CONFIDENCE_THRESHOLD = 0.70

# Class names that trigger auto-spray (partial match)
TRIGGER_CLASSES = {"fungus", "pest", "disease", "blight", "mold"}

# YOLOv8 model path (change to custom model after training)
MODEL_PATH = "yolov8n.pt"

# Alternative models (uncomment to use):
# MODEL_PATH = "yolov8s.pt"  # More accurate, slower
# MODEL_PATH = "yolov8n-seg.pt"  # With segmentation
# MODEL_PATH = "runs/detect/train/weights/best.pt"  # Custom trained

# ============================================================
# Advanced Configuration
# ============================================================
# inference size (larger = more accurate, slower)
INFERENCE_SIZE = 640

# Number of frame buffers for streaming
FRAME_BUFFER_COUNT = 2

# Enable debug logging
DEBUG = True
