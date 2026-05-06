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

# Reference to the running asyncio event loop (set during lifespan startup)
_main_event_loop: Optional[asyncio.AbstractEventLoop] = None

# ============================================================
# YOLOv8 Model
# ============================================================
log.info(f"Loading YOLO model: {MODEL_PATH}")
yolo_model = YOLO(MODEL_PATH)
log.info("Model loaded successfully!")

# Get class names for reference
CLASS_NAMES = yolo_model.names
log.info(f"Model classes ({len(CLASS_NAMES)}): {CLASS_NAMES}")


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
    """Background thread that continuously fetches frames and runs inference."""

    def __init__(self):
        super().__init__(daemon=True)
        self.running = True
        self.sync_client = httpx.Client(timeout=10.0)

    def run(self):
        global detection_paused, latest_detections, detection_count, last_frame_time

        log.info("Detection thread started")

        while self.running:
            try:
                with _state_lock:
                    paused = detection_paused

                if not paused:
                    start_time = time.time()

                    # Fetch frame from ESP32-CAM
                    try:
                        response = self.sync_client.get(ESP32CAM_CAPTURE_URL)
                    except httpx.ConnectError:
                        log.warning("Cannot connect to ESP32-CAM - retrying...")
                        time.sleep(FETCH_INTERVAL * 2)
                        continue
                    except httpx.TimeoutException:
                        log.warning("ESP32-CAM fetch timed out - retrying...")
                        time.sleep(FETCH_INTERVAL)
                        continue

                    if response.status_code != 200:
                        log.warning(f"Failed to fetch frame: HTTP {response.status_code}")
                        time.sleep(FETCH_INTERVAL)
                        continue

                    # Decode JPEG to numpy array
                    image_bytes = response.content
                    nparr = np.frombuffer(image_bytes, np.uint8)
                    frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

                    if frame is None:
                        log.warning("Failed to decode frame")
                        time.sleep(FETCH_INTERVAL)
                        continue

                    # Run YOLO inference
                    results = yolo_model(frame, verbose=False)

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
                        latest_detections = detections
                        last_frame_time = time.time()
                        detection_count += 1
                        current_count = detection_count

                    # Send to ESP32 rover for each triggered detection
                    for det in trigger_detections:
                        try:
                            rover_response = self.sync_client.post(
                                ESP32_ROVER_DETECTION_URL,
                                json={
                                    "label": det["class"],
                                    "confidence": det["confidence"]
                                },
                                headers={"Content-Type": "application/json"}
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

                # Sleep between fetches
                time.sleep(FETCH_INTERVAL)

            except Exception as e:
                log.error(f"Error in detection thread: {e}", exc_info=True)
                time.sleep(FETCH_INTERVAL)

        log.info("Detection thread stopped")

    def stop(self):
        self.running = False


# Start detection thread
detection_thread = DetectionThread()
detection_thread.start()


# ============================================================
# FastAPI Lifespan
# ============================================================
@asynccontextmanager
async def lifespan(app: FastAPI):
    global _main_event_loop
    _main_event_loop = asyncio.get_running_loop()

    log.info(f"Detection server starting on {SERVER_HOST}:{SERVER_PORT}")
    log.info(f"Camera URL: {ESP32CAM_CAPTURE_URL}")
    log.info(f"Rover URL: {ESP32_ROVER_DETECTION_URL}")
    yield
    log.info("Shutting down detection server...")
    detection_thread.stop()
    detection_thread.join(timeout=5)


app = FastAPI(title="Plant Rover Detection Server", lifespan=lifespan)


# ============================================================
# Endpoints
# ============================================================

@app.get("/")
async def root():
    """Root endpoint with server info."""
    with _state_lock:
        paused = detection_paused
        count = detection_count
    return {
        "service": "Plant Rover Detection Server",
        "status": "running",
        "endpoints": {
            "stream": "/stream",
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
    """Proxy the ESP32-CAM MJPEG stream for single-origin access."""

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
        results = yolo_model(frame, verbose=False)
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
