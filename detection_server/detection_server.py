#!/usr/bin/env python3
"""
Plant Rover Detection Server

FastAPI server that:
- Fetches frames from ESP32-CAM
- Runs YOLOv8 inference for disease/pest detection
- Forwards detections to ESP32 rover via HTTP POST
- Emits Server-Sent Events for dashboard updates
- Proxies MJPEG stream for single-origin access
"""

import asyncio
import json
import threading
import time
import logging
from contextlib import asynccontextmanager
from typing import Optional, Set
from datetime import datetime

import httpx
import uvicorn
from fastapi import FastAPI, Response, Request
from fastapi.responses import StreamingResponse, JSONResponse
from sse_starlette.sse import EventSourceResponse
from ultralytics import YOLO
import cv2
import numpy as np

# ============================================================
# Configuration
# ============================================================
# Import from config.py if exists, otherwise use defaults
try:
    from config import *
except ImportError:
    # Default configuration
    ESP32CAM_CAPTURE_URL = "http://192.168.4.2:81/capture"
    ESP32CAM_STREAM_URL = "http://192.168.4.2:81/stream"
    ESP32_ROVER_DETECTION_URL = "http://192.168.4.1/detection"
    SERVER_HOST = "0.0.0.0"
    SERVER_PORT = 5000
    FETCH_INTERVAL = 0.5
    CONFIDENCE_THRESHOLD = 0.70
    TRIGGER_CLASSES = {"fungus", "pest"}
    MODEL_PATH = "yolov8n.pt"
    DEBUG = True

# ============================================================
# Logging
# ============================================================
logging.basicConfig(
    level=logging.DEBUG if DEBUG else logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s"
)
log = logging.getLogger("detection")

# ============================================================
# Global State (thread-safe via lock)
# ============================================================
_state_lock = threading.Lock()
detection_paused = False
latest_detections = []
last_frame_time = 0.0
detection_count = 0

# Annotated frame buffer for MJPEG streaming (bounding boxes drawn)
_annotated_lock = threading.Lock()
_latest_annotated_jpeg = None  # bytes of the latest annotated JPEG
_annotated_fps = 0.0

# Reference to the running asyncio event loop (set during lifespan startup)
_main_event_loop: Optional[asyncio.AbstractEventLoop] = None

# ============================================================
# YOLOv8 Model
# ============================================================
try:
    log.info(f"Loading YOLO model: {MODEL_PATH}")
    yolo_model = YOLO(MODEL_PATH)
    log.info("Model loaded successfully!")
    CLASS_NAMES = yolo_model.names
    log.info(f"Model classes ({len(CLASS_NAMES)}): {CLASS_NAMES}")
except Exception as e:
    log.error(f"FATAL: Failed to load YOLO model '{MODEL_PATH}': {e}")
    log.error("Make sure the model file exists. For the trained plant detector, copy it to:")
    log.error("  detection_server/../plant_rover_training/models/plant_detector.pt")
    log.error("Or update MODEL_PATH in config.py to point to the correct file.")
    raise SystemExit(1)


# ============================================================
# SSE Event Manager
# ============================================================
class SSEManager:
    """Manages Server-Sent Event connections."""

    def __init__(self):
        self._queues: Set[asyncio.Queue] = set()
        self._lock = asyncio.Lock()

    async def subscribe(self) -> asyncio.Queue:
        queue = asyncio.Queue()
        async with self._lock:
            self._queues.add(queue)
        return queue

    async def unsubscribe(self, queue: asyncio.Queue):
        async with self._lock:
            self._queues.discard(queue)

    async def broadcast(self, message: str):
        async with self._lock:
            for q in self._queues:
                await q.put(message)

    def broadcast_threadsafe(self, message: str):
        """Call from a background thread to broadcast to all SSE clients."""
        if _main_event_loop is None:
            log.warning("Cannot broadcast: event loop not set")
            return
        async def _do():
            await self.broadcast(message)
        asyncio.run_coroutine_threadsafe(_do(), _main_event_loop)


sse_manager = SSEManager()


# ============================================================
# Detection Thread
# ============================================================
class DetectionThread(threading.Thread):
    """Background thread that continuously fetches frames and runs inference.
    
    Uses the MJPEG stream from ESP32-CAM (not /capture) because the ESP32-CAM's
    WebServer is single-threaded -- while a stream client is connected, /capture
    requests get blocked.
    """

    def __init__(self):
        super().__init__(daemon=True)
        self.running = True

    def run(self):
        global detection_paused, latest_detections, detection_count, last_frame_time

        log.info("Detection thread started")

        while self.running:
            try:
                with _state_lock:
                    paused = detection_paused

                if paused:
                    time.sleep(FETCH_INTERVAL)
                    continue

                # Connect to ESP32-CAM MJPEG stream and parse frames
                self._consume_mjpeg_stream()

            except Exception as e:
                log.error(f"Error in detection thread: {e}", exc_info=True)
                time.sleep(FETCH_INTERVAL * 2)

        log.info("Detection thread stopped")

    def _consume_mjpeg_stream(self):
        """Connect to the ESP32-CAM MJPEG stream and extract JPEG frames."""
        stream_url = ESP32CAM_STREAM_URL
        log.info(f"Connecting to MJPEG stream: {stream_url}")

        try:
            # Use httpx sync streaming to read the MJPEG stream
            with httpx.stream("GET", stream_url, timeout=30.0) as response:
                if response.status_code != 200:
                    log.warning(f"Stream returned HTTP {response.status_code}")
                    return

                buffer = b""
                in_frame = False

                for chunk in response.iter_bytes(chunk_size=4096):
                    if not self.running:
                        break

                    with _state_lock:
                        paused = detection_paused

                    if paused:
                        continue

                    buffer += chunk

                    # Parse MJPEG frames from the buffer
                    while True:
                        if not in_frame:
                            # Look for the start of a JPEG frame after boundary
                            jpeg_start = buffer.find(b'\xff\xd8')
                            if jpeg_start == -1:
                                buffer = buffer[-4:]  # Keep last bytes in case of split marker
                                break
                            buffer = buffer[jpeg_start:]
                            in_frame = True

                        if in_frame:
                            # Look for JPEG end marker
                            jpeg_end = buffer.find(b'\xff\xd9')
                            if jpeg_end == -1:
                                # Need more data
                                # Prevent buffer from growing unbounded
                                if len(buffer) > 500000:
                                    log.warning("MJPEG buffer too large, resetting")
                                    buffer = b""
                                    in_frame = False
                                break

                            # Extract the complete JPEG frame
                            jpeg_data = buffer[:jpeg_end + 2]
                            buffer = buffer[jpeg_end + 2:]
                            in_frame = False

                            # Process this frame
                            self._process_frame(jpeg_data)

        except httpx.ConnectError:
            log.warning("Cannot connect to ESP32-CAM stream - retrying...")
            time.sleep(FETCH_INTERVAL * 2)
        except httpx.TimeoutException:
            log.warning("ESP32-CAM stream timed out - reconnecting...")
        except Exception as e:
            log.error(f"MJPEG stream error: {e}")

    def _process_frame(self, jpeg_data):
        """Decode a JPEG frame, run YOLO, annotate, and update state."""
        start_time = time.time()

        # Decode JPEG to numpy array
        nparr = np.frombuffer(jpeg_data, np.uint8)
        frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

        if frame is None:
            return

        # Run YOLO inference
        results = yolo_model(frame, verbose=False, imgsz=INFERENCE_SIZE)

        # ── Annotate frame with bounding boxes ──
        annotated_frame = frame.copy()
        for result in results:
            boxes = result.boxes
            if boxes is not None and len(boxes) > 0:
                for box in boxes:
                    cls_id = int(box.cls[0])
                    conf = float(box.conf[0])
                    cls_name = CLASS_NAMES[cls_id]

                    # Bounding box coordinates
                    x1, y1, x2, y2 = map(int, box.xyxy[0].tolist())

                    # Color based on class
                    cls_lower = cls_name.lower()
                    is_trigger = any(t in cls_lower for t in TRIGGER_CLASSES)
                    if is_trigger and conf > CONFIDENCE_THRESHOLD:
                        color = (0, 0, 255)    # Red = high-priority detection
                        thickness = 3
                    elif is_trigger:
                        color = (0, 165, 255)   # Orange = low-confidence trigger
                        thickness = 2
                    else:
                        color = (0, 255, 0)     # Green = normal detection
                        thickness = 2

                    # Draw bounding box
                    cv2.rectangle(annotated_frame, (x1, y1), (x2, y2), color, thickness)

                    # Label with class name + confidence
                    label_text = f"{cls_name} {conf:.1%}"
                    font_scale = 0.6
                    font_thickness = 1
                    (tw, th), _ = cv2.getTextSize(label_text, cv2.FONT_HERSHEY_SIMPLEX, font_scale, font_thickness)
                    # Background rectangle for text
                    cv2.rectangle(annotated_frame, (x1, y1 - th - 10), (x1 + tw + 4, y1), color, -1)
                    cv2.putText(annotated_frame, label_text, (x1 + 2, y1 - 5),
                                cv2.FONT_HERSHEY_SIMPLEX, font_scale, (255, 255, 255), font_thickness, cv2.LINE_AA)

        # Encode annotated frame as JPEG
        encode_params = [cv2.IMWRITE_JPEG_QUALITY, 80]
        ok, annotated_jpeg = cv2.imencode('.jpg', annotated_frame, encode_params)
        if ok:
            with _annotated_lock:
                global _latest_annotated_jpeg
                _latest_annotated_jpeg = annotated_jpeg.tobytes()

        # Process detections
        detections = []
        trigger_detections = []

        for result in results:
            boxes = result.boxes
            if boxes is not None:
                for box in boxes:
                    cls_id = int(box.cls[0])
                    conf = float(box.conf[0])
                    cls_name = CLASS_NAMES[cls_id]

                    detection = {
                        "class": cls_name,
                        "confidence": round(conf, 3),
                        "timestamp": datetime.utcnow().isoformat() + "Z"
                    }
                    detections.append(detection)

                    # Check if this detection should trigger spray
                    cls_lower = cls_name.lower()
                    if (conf > CONFIDENCE_THRESHOLD and
                            any(trigger in cls_lower for trigger in TRIGGER_CLASSES)):
                        trigger_detections.append(detection)

        # Update shared state under lock
        with _state_lock:
            global latest_detections, last_frame_time
            latest_detections = detections
            last_frame_time = time.time()
            detection_count += 1
            current_count = detection_count

        # Send to ESP32 rover for each triggered detection
        for det in trigger_detections:
            try:
                rover_response = httpx.post(
                    ESP32_ROVER_DETECTION_URL,
                    json={
                        "label": det["class"],
                        "confidence": det["confidence"]
                    },
                    headers={"Content-Type": "application/json"},
                    timeout=5.0
                )
                log.info(f"Sent to rover: {det} -> HTTP {rover_response.status_code}")
            except Exception as e:
                log.warning(f"Failed to send to rover: {e}")

            # Broadcast SSE event (threadsafe)
            sse_manager.broadcast_threadsafe(json.dumps(det))

        # Log periodically
        if current_count % 10 == 0:
            elapsed = time.time() - start_time
            fps = 1.0 / max(elapsed, 0.001)
            log.info(
                f"Frame {current_count}: {len(detections)} detections, "
                f"{fps:.1f} FPS, paused={paused}"
            )

    def stop(self):
        self.running = False


# Create detection thread (started in lifespan startup to avoid double-start)
detection_thread = DetectionThread()

# ============================================================
# FastAPI Lifespan
# ============================================================
@asynccontextmanager
async def lifespan(app):
    global _main_event_loop
    _main_event_loop = asyncio.get_running_loop()

    # Start the detection background thread
    detection_thread.start()

    log.info(f"Detection server starting on {SERVER_HOST}:{SERVER_PORT}")
    log.info(f"Camera URL: {ESP32CAM_CAPTURE_URL}")
    log.info(f"Rover URL: {ESP32_ROVER_DETECTION_URL}")
    yield
    log.info("Shutting down detection server...")
    detection_thread.stop()
    detection_thread.join(timeout=5)

app = FastAPI(title="Plant Rover Detection Server", lifespan=lifespan)

# Serve the dashboard HTML at root
from fastapi.responses import HTMLResponse
from pathlib import Path

_DASHBOARD_PATH = Path(__file__).parent.parent / "plant_rover_arduino" / "dashboard.html"

@app.get("/", response_class=HTMLResponse)
async def dashboard():
    if _DASHBOARD_PATH.exists():
        return _DASHBOARD_PATH.read_text(encoding="utf-8")
    return HTMLResponse("<h1>Dashboard not found</h1><p>Expected at plant_rover_arduino/dashboard.html</p>", status_code=404)


# ============================================================
# Endpoints
# ============================================================

@app.get("/status")
async def root():
    """Server status info."""
    with _state_lock:
        paused = detection_paused
        count = detection_count
    return {
        "service": "Plant Rover Detection Server",
        "status": "running",
        "endpoints": {
            "dashboard": "/ (HTML dashboard)",
            "status": "/status (this JSON)",
            "stream": "/stream (raw camera)",
            "annotated_stream": "/annotated_stream (with bounding boxes)",
            "annotated_capture": "/annotated_capture (single frame)",
            "events": "/events",
            "pause": "/pause (POST)",
            "resume": "/resume (POST)",
            "detections": "/detections",
            "detect": "/detect (POST)",
        },
        "camera": ESP32CAM_CAPTURE_URL,
        "paused": paused,
        "model": MODEL_PATH,
        "detection_count": count
    }


@app.get("/detections")
async def get_detections():
    """Get latest detections."""
    with _state_lock:
        dets = list(latest_detections)
        ft = last_frame_time
        paused = detection_paused
        count = detection_count
    return {
        "count": len(dets),
        "detections": dets,
        "last_frame_time": ft,
        "paused": paused,
        "detection_count": count
    }


@app.post("/pause")
async def pause_detection():
    """Pause detection processing."""
    global detection_paused
    with _state_lock:
        detection_paused = True
    return {"status": "paused", "message": "Detection paused"}


@app.post("/resume")
async def resume_detection():
    """Resume detection processing."""
    global detection_paused
    with _state_lock:
        detection_paused = False
    return {"status": "resumed", "message": "Detection resumed"}


@app.get("/events")
async def detection_events(request: Request):
    """Server-Sent Events endpoint for real-time detection updates."""
    queue = await sse_manager.subscribe()
    try:
        while True:
            if await request.is_disconnected():
                break
            try:
                data = await asyncio.wait_for(queue.get(), timeout=1.0)
                yield {"event": "detection", "data": data}
            except asyncio.TimeoutError:
                yield {"event": "keepalive", "data": ""}
    finally:
        await sse_manager.unsubscribe(queue)


@app.get("/stream")
async def proxy_stream():
    """Proxy the raw ESP32-CAM MJPEG stream (no bounding boxes)."""

    async def generate():
        async with httpx.AsyncClient(timeout=30.0) as client:
            async with client.stream("GET", ESP32CAM_STREAM_URL) as response:
                if response.status_code != 200:
                    yield f"Error: Unable to connect to camera ({response.status_code})".encode()
                    return

                async for chunk in response.aiter_bytes():
                    yield chunk

    return StreamingResponse(
        generate(),
        media_type="multipart/x-mixed-replace; boundary=frame"
    )


@app.get("/annotated_stream")
async def annotated_stream():
    """MJPEG stream of annotated frames with YOLO bounding boxes drawn."""

    boundary = "annotated_boundary"

    async def generate():
        while True:
            with _annotated_lock:
                jpeg_data = _latest_annotated_jpeg

            if jpeg_data is None:
                # No frame yet -- send a black placeholder
                placeholder = np.zeros((480, 640, 3), dtype=np.uint8)
                cv2.putText(placeholder, "Waiting for camera...", (150, 250),
                            cv2.FONT_HERSHEY_SIMPLEX, 1.0, (100, 100, 100), 2, cv2.LINE_AA)
                ok, buf = cv2.imencode('.jpg', placeholder)
                jpeg_data = buf.tobytes() if ok else b''

            # MJPEG frame
            frame_header = (
                f"--{boundary}\r\n"
                f"Content-Type: image/jpeg\r\n"
                f"Content-Length: {len(jpeg_data)}\r\n"
                f"\r\n"
            ).encode()
            yield frame_header + jpeg_data + b"\r\n"

            # Wait before sending next frame (~10-15 FPS for annotated)
            await asyncio.sleep(0.07)

    return StreamingResponse(
        generate(),
        media_type=f"multipart/x-mixed-replace; boundary={boundary}",
    )


@app.get("/annotated_capture")
async def annotated_capture():
    """Single annotated JPEG snapshot with bounding boxes."""
    with _annotated_lock:
        jpeg_data = _latest_annotated_jpeg

    if jpeg_data is None:
        return JSONResponse({"error": "No annotated frame available yet"}, status_code=503)

    return Response(content=jpeg_data, media_type="image/jpeg")


@app.post("/detect")
async def manual_detect(request: Request):
    """Manually trigger a single detection on the current frame."""
    global latest_detections, last_frame_time, detection_count

    try:
        # Fetch frame
        async with httpx.AsyncClient(timeout=10.0) as client:
            response = await client.get(ESP32CAM_CAPTURE_URL)
            if response.status_code != 200:
                return JSONResponse(
                    {"error": f"Failed to fetch frame: HTTP {response.status_code}"},
                    status_code=502
                )

            image_bytes = response.content

        # Decode
        nparr = np.frombuffer(image_bytes, np.uint8)
        frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

        if frame is None:
            return JSONResponse({"error": "Failed to decode frame"}, status_code=500)

        # Run inference
        results = yolo_model(frame, verbose=False, imgsz=INFERENCE_SIZE)
        detections = []

        for result in results:
            boxes = result.boxes
            if boxes is not None:
                for box in boxes:
                    cls_id = int(box.cls[0])
                    conf = float(box.conf[0])
                    detections.append({
                        "class": CLASS_NAMES[cls_id],
                        "confidence": round(conf, 3)
                    })

        with _state_lock:
            latest_detections = detections
            last_frame_time = time.time()
            detection_count += 1

        return JSONResponse({
            "success": True,
            "detections": detections,
            "count": len(detections)
        })

    except Exception as e:
        log.error(f"Manual detect error: {e}", exc_info=True)
        return JSONResponse({"error": str(e)}, status_code=500)


@app.post("/model/load")
async def load_model(request: Request):
    """Load a different YOLOv8 model."""
    global yolo_model, CLASS_NAMES, MODEL_PATH

    data = await request.json()
    model_path = data.get("model_path")

    if not model_path:
        return JSONResponse({"error": "model_path required"}, status_code=400)

    try:
        new_model = YOLO(model_path)
        yolo_model = new_model
        CLASS_NAMES = yolo_model.names
        MODEL_PATH = model_path

        return JSONResponse({
            "success": True,
            "model": model_path,
            "classes": CLASS_NAMES
        })
    except Exception as e:
        return JSONResponse({"error": f"Failed to load model: {e}"}, status_code=500)


# ============================================================
# Main Entry Point
# ============================================================
if __name__ == "__main__":
    uvicorn.run(
        "detection_server:app",
        host=SERVER_HOST,
        port=SERVER_PORT,
        reload=False,  # reload=True can cause issues with background threads
        log_level="info"
    )
