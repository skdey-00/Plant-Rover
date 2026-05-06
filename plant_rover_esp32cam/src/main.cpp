/**
 * ESP32-CAM MJPEG Stream Server
 * Board: AiThinker ESP32-CAM
 *
 * Features:
 * - Connects to WiFi AP "PlantRover"
 * - MJPEG stream on port 81 at /stream (supports multiple concurrent clients)
 * - Single frame capture at /capture (works even while streaming)
 * - Resolution: SVGA (800x600)
 * - JPEG Quality: 12
 *
 * Architecture: Non-blocking multi-client HTTP server.
 * The loop() reads incoming requests without blocking, so /capture
 * can be served even while one or more /stream clients are connected.
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClient.h>

// ============================================================
// WiFi Configuration
// ============================================================
const char* WIFI_SSID = "PlantRover";
const char* WIFI_PASSWORD = "rover1234";
const int STREAM_PORT = 81;

WiFiServer server(STREAM_PORT);

// ============================================================
// Multi-client stream state
// ============================================================
#define MAX_STREAM_CLIENTS 4

struct StreamClient {
    WiFiClient client;
    bool active = false;
    unsigned long lastFrameTime = 0;
    int frameCount = 0;
};

StreamClient streamClients[MAX_STREAM_CLIENTS];

// Frame timing
const int FRAME_INTERVAL_MS = 50;  // ~20 FPS max per client

// ============================================================
// Camera Model Configuration (AiThinker ESP32-CAM)
// ============================================================
#define CAMERA_MODEL_AI_THINKER

// Pin definitions for AiThinker ESP32-CAM
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ============================================================
// Camera Configuration
// ============================================================
camera_config_t config = {
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sccb_sda = SIOD_GPIO_NUM,
    .pin_sccb_scl = SIOC_GPIO_NUM,

    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,
    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,

    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_SVGA,    // 800x600
    .jpeg_quality = 12,
    .fb_count = 2,
};

// ============================================================
// HTTP Response Helpers
// ============================================================
void sendStreamHeader(WiFiClient& client) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
    client.println("Access-Control-Allow-Origin: *");
    client.println("Cache-Control: no-cache, no-store, must-revalidate");
    client.println("Pragma: no-cache");
    client.println("Expires: 0");
    client.println();
}

void sendCaptureHeader(WiFiClient& client, size_t len) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: image/jpeg");
    client.println("Content-Disposition: inline; filename=capture.jpg");
    client.printf("Content-Length: %u\r\n", len);
    client.println("Access-Control-Allow-Origin: *");
    client.println();
}

void sendError(WiFiClient& client, int code, const char* msg) {
    client.printf("HTTP/1.1 %d %s\r\n", code, msg);
    client.println("Content-Type: text/plain");
    client.println("Access-Control-Allow-Origin: *");
    client.println();
    client.println(msg);
}

// ============================================================
// Find a free stream slot
// ============================================================
int findFreeStreamSlot() {
    for (int i = 0; i < MAX_STREAM_CLIENTS; i++) {
        if (!streamClients[i].active) return i;
    }
    return -1;  // All slots full
}

// ============================================================
// Start streaming to a client
// ============================================================
void startStreamClient(WiFiClient& client) {
    int slot = findFreeStreamSlot();
    if (slot < 0) {
        sendError(client, 503, "Server busy - max stream clients reached");
        client.stop();
        Serial.println("Stream rejected: all slots busy");
        return;
    }

    Serial.printf("Stream client #%d connected (slot %d)\n",
                   client.fd() != -1 ? client.fd() : 0, slot);

    sendStreamHeader(client);

    streamClients[slot].client = client;
    streamClients[slot].active = true;
    streamClients[slot].lastFrameTime = 0;
    streamClients[slot].frameCount = 0;
}

// ============================================================
// Single Capture Handler
// ============================================================
void handleCapture(WiFiClient& client) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        sendError(client, 500, "Camera capture failed");
        return;
    }

    sendCaptureHeader(client, fb->len);
    client.write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
}

// ============================================================
// Parse HTTP request path from a request line like "GET /path HTTP/1.1"
// ============================================================
String parsePath(const String& requestLine) {
    int start = requestLine.indexOf(' ');
    if (start == -1) return "";
    int end = requestLine.indexOf(' ', start + 1);
    if (end == -1) return "";
    return requestLine.substring(start + 1, end);
}

// ============================================================
// Handle new incoming connection (non-blocking)
// ============================================================
void handleNewClient(WiFiClient newClient) {
    // Set a short timeout so we don't block loop()
    newClient.setTimeout(500);

    // Wait briefly for the request line (non-blocking check)
    unsigned long waitStart = millis();
    while (!newClient.available() && newClient.connected()) {
        if (millis() - waitStart > 1000) {
            newClient.stop();
            return;
        }
        delay(1);
    }

    if (!newClient.available()) {
        newClient.stop();
        return;
    }

    String requestLine = newClient.readStringUntil('\r');
    requestLine.trim();

    if (requestLine.length() == 0) {
        newClient.stop();
        return;
    }

    String path = parsePath(requestLine);

    if (path == "/stream") {
        startStreamClient(newClient);
        // Don't stop the client - it's now managed as a stream client
    }
    else if (path == "/capture") {
        handleCapture(newClient);
        newClient.stop();
    }
    else {
        sendError(newClient, 404, "Not Found. Try /stream or /capture");
        newClient.stop();
    }
}

// ============================================================
// Update all active stream clients (send frames)
// ============================================================
void updateStreamClients() {
    unsigned long now = millis();

    for (int i = 0; i < MAX_STREAM_CLIENTS; i++) {
        if (!streamClients[i].active) continue;

        WiFiClient& client = streamClients[i].client;

        // Check if client disconnected
        if (!client.connected()) {
            Serial.printf("Stream slot %d disconnected (sent %d frames)\n",
                          i, streamClients[i].frameCount);
            client.stop();
            streamClients[i].active = false;
            continue;
        }

        // Throttle frame rate
        if (now - streamClients[i].lastFrameTime < FRAME_INTERVAL_MS) {
            continue;
        }

        // Capture frame
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            // Frame failed - skip this round, don't disconnect
            continue;
        }

        // Send multipart frame
        client.println("--frame");
        client.println("Content-Type: image/jpeg");
        client.printf("Content-Length: %u\r\n\r\n", fb->len);
        client.write(fb->buf, fb->len);
        client.println();

        esp_camera_fb_return(fb);

        streamClients[i].lastFrameTime = now;
        streamClients[i].frameCount++;

        // Periodic log
        if (streamClients[i].frameCount % 100 == 0) {
            Serial.printf("Stream slot %d: frame %d\n", i, streamClients[i].frameCount);
        }
    }
}

// ============================================================
// Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n=================================");
    Serial.println("ESP32-CAM Stream Server (Multi-client)");
    Serial.println("=================================\n");

    // Initialize camera
    Serial.println("Initializing camera...");
    esp_err_t err = esp_camera_init(&config);

    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        Serial.println("Common fixes:");
        Serial.println("  - Check camera module is properly connected");
        Serial.println("  - Try resetting the camera power");
        Serial.println("  - Reduce XCLK frequency to 10MHz");
        return;
    }

    Serial.println("Camera initialized successfully");

    // Get camera sensor and apply settings
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_brightness(sensor, 0);
        sensor->set_contrast(sensor, 0);
        sensor->set_saturation(sensor, 0);
        sensor->set_special_effect(sensor, 0);
        sensor->set_whitebal(sensor, 1);
        sensor->set_awb_gain(sensor, 1);
        sensor->set_wb_mode(sensor, 0);
        sensor->set_exposure_ctrl(sensor, 1);
        sensor->set_aec2(sensor, 0);
        sensor->set_ae_level(sensor, 0);
        sensor->set_aec_value(sensor, 300);
        sensor->set_gain_ctrl(sensor, 1);
        sensor->set_agc_gain(sensor, 0);
        sensor->set_gainceiling(sensor, 0);
        sensor->set_bpc(sensor, 0);
        sensor->set_wpc(sensor, 1);
        sensor->set_raw_gma(sensor, 1);
        sensor->set_lenc(sensor, 1);
        sensor->set_hmirror(sensor, 0);
        sensor->set_vflip(sensor, 0);
        sensor->set_dcw(sensor, 1);
        sensor->set_colorbar(sensor, 0);
    }

    // Connect to WiFi
    Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nWiFi connection failed!");
        Serial.println("Please check:");
        Serial.println("  - ESP32-CAM is close to the AP");
        Serial.println("  - SSID and password are correct");
        Serial.println("  - PlantRover AP is active");
        return;
    }

    Serial.println("\nWiFi connected!");
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());

    // Start server
    server.begin();
    server.setNoDelay(true);
    Serial.printf("Stream server started on port %d\n", STREAM_PORT);

    Serial.println("\n=================================");
    Serial.println("Stream URLs:");
    Serial.println("=================================");
    Serial.printf("MJPEG Stream:  http://%s:%d/stream\n",
                  WiFi.localIP().toString().c_str(), STREAM_PORT);
    Serial.printf("Single Frame: http://%s:%d/capture\n",
                  WiFi.localIP().toString().c_str(), STREAM_PORT);
    Serial.printf("Max clients:   %d\n", MAX_STREAM_CLIENTS);
    Serial.println("=================================\n");
    Serial.println("Ready to stream!\n");
}

// ============================================================
// Main Loop (non-blocking)
// ============================================================
void loop() {
    // Check for new incoming clients (non-blocking)
    WiFiClient newClient = server.available();
    if (newClient) {
        handleNewClient(newClient);
    }

    // Push frames to all active stream clients
    updateStreamClients();

    delay(1);
}
