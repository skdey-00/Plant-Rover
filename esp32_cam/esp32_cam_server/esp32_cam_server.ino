/**
 * Plant Rover - ESP32-CAM Module
 *
 * Connects to the PlantRover WiFi AP and serves:
 * - MJPEG stream on /stream (port 81)
 * - Single-frame JPEG capture on /capture (port 81)
 *
 * Uses raw WiFiClient for MJPEG streaming (WebServer sendContent is unreliable)
 *
 * Hardware:
 * - AI-Thinker ESP32-CAM with OV2640 + PSRAM
 *
 * Network:
 * - Connects to "PlantRover" AP (ESP32 rover at 192.168.4.1)
 * - Gets IP via DHCP (check Serial monitor for actual IP)
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

// ============================================================
// Camera Model - AI-Thinker ESP32-CAM
// ============================================================
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// ============================================================
// WiFi Configuration (must match the rover's AP)
// ============================================================
const char* WIFI_SSID = "PlantRover";
const char* WIFI_PASSWORD = "rover1234";

// Camera server on port 81
WebServer camServer(81);

// ============================================================
// Camera Configuration
// ============================================================
framesize_t FRAME_SIZE = FRAMESIZE_VGA;
int JPEG_QUALITY = 12;

// MJPEG boundary
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ============================================================
// Camera Init
// ============================================================
bool initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    if (psramFound()) {
        Serial.println("PSRAM found");
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 10;
        config.fb_count = 2;
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init FAILED: 0x%x\n", err);
        return false;
    }

    sensor_t* s = esp_camera_sensor_get();
    s->set_framesize(s, FRAME_SIZE);
    s->set_quality(s, JPEG_QUALITY);
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);

    Serial.println("Camera initialized!");
    return true;
}

// ============================================================
// MJPEG Stream Handler (raw WiFiClient -- NOT sendContent)
// ============================================================
void handleStream() {
    Serial.println("Stream client connected");

    // Grab a fresh frame to start
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        camServer.send(500, "text/plain", "Camera capture failed");
        return;
    }
    esp_camera_fb_return(fb);

    // Get the raw WiFiClient from the server
    WiFiClient client = camServer.client();

    // Send HTTP response headers manually
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: multipart/x-mixed-replace; boundary=" PART_BOUNDARY);
    client.println("Access-Control-Allow-Origin: *");
    client.println("Connection: close");
    client.println();

    int frameCount = 0;

    while (client.connected()) {
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("Stream: capture failed");
            break;
        }

        // Send boundary
        client.print(_STREAM_BOUNDARY);

        // Send part header with content length
        char partBuf[64];
        snprintf(partBuf, 64, _STREAM_PART, fb->len);
        client.print(partBuf);

        // Send JPEG data
        client.write(fb->buf, fb->len);

        // Return frame buffer
        esp_camera_fb_return(fb);

        frameCount++;
        if (frameCount % 100 == 0) {
            Serial.printf("Stream: %d frames sent\n", frameCount);
        }

        // Small delay -- ~15-20 FPS at VGA
        delay(30);
    }

    client.stop();
    Serial.println("Stream client disconnected");
}

// ============================================================
// Single Capture Handler
// ============================================================
void handleCapture() {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        camServer.send(500, "application/json", "{\"error\":\"Camera capture failed\"}");
        return;
    }

    // Write raw HTTP response directly to client (bypass WebServer.send
    // which sets Content-Length:0 for empty string bodies)
    WiFiClient client = camServer.client();
    char headers[256];
    snprintf(headers, sizeof(headers),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %u\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        (unsigned int)fb->len);
    client.write(headers, strlen(headers));
    client.write((const char*)fb->buf, fb->len);

    esp_camera_fb_return(fb);
}

// ============================================================
// Settings Endpoint
// ============================================================
void handleSettings() {
    if (camServer.hasArg("quality")) {
        int q = camServer.arg("quality").toInt();
        if (q >= 1 && q <= 63) {
            JPEG_QUALITY = q;
            sensor_t* s = esp_camera_sensor_get();
            if (s) s->set_quality(s, q);
        }
    }

    if (camServer.hasArg("framesize")) {
        String fs = camServer.arg("framesize");
        sensor_t* s = esp_camera_sensor_get();
        if (s) {
            if (fs == "QVGA")  { s->set_framesize(s, FRAMESIZE_QVGA); FRAME_SIZE = FRAMESIZE_QVGA; }
            else if (fs == "CIF")  { s->set_framesize(s, FRAMESIZE_CIF); FRAME_SIZE = FRAMESIZE_CIF; }
            else if (fs == "VGA")  { s->set_framesize(s, FRAMESIZE_VGA); FRAME_SIZE = FRAMESIZE_VGA; }
            else if (fs == "SVGA") { s->set_framesize(s, FRAMESIZE_SVGA); FRAME_SIZE = FRAMESIZE_SVGA; }
            else if (fs == "XGA")  { s->set_framesize(s, FRAMESIZE_XGA); FRAME_SIZE = FRAMESIZE_XGA; }
        }
    }

    String json = "{";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"quality\":" + String(JPEG_QUALITY) + ",";
    json += "\"psram\":" + String(psramFound() ? "true" : "false") + ",";
    json += "\"ssid\":\"" + String(WIFI_SSID) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
    json += "\"free_heap\":" + String(ESP.getFreeHeap());
    json += "}";
    camServer.send(200, "application/json", json);
}

// ============================================================
// Root page
// ============================================================
void handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>Plant Rover - ESP32-CAM</title>";
    html += "<style>";
    html += "body{font-family:monospace;background:#1a1a2e;color:#4ecdc4;padding:30px;text-align:center}";
    html += "h1{font-size:2rem;text-shadow:0 0 15px rgba(78,205,196,0.5)}";
    html += ".info{background:rgba(255,255,255,0.1);border-radius:10px;padding:20px;margin:20px auto;max-width:500px;text-align:left}";
    html += "a{color:#4ecdc4;text-decoration:none;padding:10px 20px;background:rgba(78,205,196,0.2);border-radius:5px;display:inline-block;margin:10px}";
    html += "a:hover{background:rgba(78,205,196,0.4)}";
    html += "img{max-width:100%;border-radius:10px;margin:20px 0}";
    html += "</style></head><body>";
    html += "<h1>ESP32-CAM</h1>";
    html += "<div class='info'>";
    html += "<p><strong>IP:</strong> " + WiFi.localIP().toString() + "</p>";
    html += "<p><strong>Port:</strong> 81</p>";
    html += "<p><strong>WiFi:</strong> " + String(WIFI_SSID) + "</p>";
    html += "<p><strong>RSSI:</strong> " + String(WiFi.RSSI()) + " dBm</p>";
    html += "<p><strong>PSRAM:</strong> " + String(psramFound() ? "Yes" : "No") + "</p>";
    html += "<p><strong>Free Heap:</strong> " + String(ESP.getFreeHeap()) + "</p>";
    html += "</div>";
    html += "<a href='/capture'>Capture</a> <a href='/stream'>Stream</a>";
    html += "<br><img src='/stream' style='max-width:640px'>";
    html += "</body></html>";
    camServer.send(200, "text/html", html);
}

// ============================================================
// Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=================================");
    Serial.println("ESP32-CAM - Plant Rover Camera");
    Serial.println("=================================\n");

    // Init camera FIRST
    Serial.println("Initializing camera...");
    if (!initCamera()) {
        Serial.println("FATAL: Camera init failed! Halting.");
        while (1) delay(1000);
    }

    // Connect WiFi
    Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        attempts++;
        if (attempts > 40) {
            Serial.println("\nWiFi timeout! Restarting...");
            ESP.restart();
        }
    }

    Serial.println("\nWiFi connected!");
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());

    // IMPORTANT: Print the actual IP for config.py
    Serial.println("\n========================================");
    Serial.printf("UPDATE config.py:\n");
    Serial.printf("  ESP32CAM_CAPTURE_URL = \"http://%s:81/capture\"\n", WiFi.localIP().toString().c_str());
    Serial.printf("  ESP32CAM_STREAM_URL  = \"http://%s:81/stream\"\n", WiFi.localIP().toString().c_str());
    Serial.println("========================================\n");

    // Setup routes
    camServer.on("/", HTTP_GET, handleRoot);
    camServer.on("/capture", HTTP_GET, handleCapture);
    camServer.on("/stream", HTTP_GET, handleStream);
    camServer.on("/settings", HTTP_GET, handleSettings);

    camServer.begin();
    Serial.println("Camera server ready on port 81!");
    Serial.printf("  http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.printf("  http://%s:81/capture\n", WiFi.localIP().toString().c_str());
    Serial.printf("  http://%s:81/stream\n", WiFi.localIP().toString().c_str());
    Serial.println();
}

// ============================================================
// Loop
// ============================================================
unsigned long lastStatusPrint = 0;

void loop() {
    camServer.handleClient();

    unsigned long now = millis();
    if (now - lastStatusPrint > 30000) {
        lastStatusPrint = now;
        Serial.printf("[CAM] Heap: %u, RSSI: %d, IP: %s\n",
                      ESP.getFreeHeap(), WiFi.RSSI(),
                      WiFi.localIP().toString().c_str());

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi lost! Reconnecting...");
            WiFi.reconnect();
        }
    }

    delay(2);
}
