/**
 * Plant Rover - ESP32 WiFi Controlled Robot
 *
 * Hardware:
 * - ESP32 WROOM
 * - 3x Servos on GPIO 18, 19, 21
 * - L298N Motor Driver:
 *   - IN1: GPIO 25, IN2: GPIO 26 (Motor A - Left)
 *   - IN3: GPIO 27, IN4: GPIO 14 (Motor B - Right)
 *   - ENA: GPIO 32 (PWM Left)
 *   - ENB: GPIO 33 (PWM Right)
 *
 * Features:
 * - WiFi AP: "PlantRover" / "rover1234"
 * - HTTP Server on port 80 (HTML Dashboard)
 * - WebSocket Server on port 81 (Real-time control + JSON commands)
 * - Differential steering with x/y joystick input
 * - Automatic spray system based on detection signals
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

// ============================================================
// WiFi & Network Configuration
// ============================================================
const char* AP_SSID = "PlantRover";
const char* AP_PASSWORD = "rover1234";
const int HTTP_PORT = 80;
const int WS_PORT = 81;

IPAddress localIP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

WebServer httpServer(HTTP_PORT);
WebSocketsServer webSocket = WebSocketsServer(WS_PORT);

// ============================================================
// Servo Configuration
// ============================================================
const int SERVO1_PIN = 18;
const int SERVO2_PIN = 19;
const int SERVO3_PIN = 21;

Servo servo1;
Servo servo2;
Servo servo3;

int servo1Angle = 90;
int servo2Angle = 0;
int servo3Angle = 0;

// ============================================================
// Motor Driver (L298N) Configuration
// ============================================================
// Motor A (Left)
const int MOTOR_A_IN1 = 25;
const int MOTOR_A_IN2 = 26;
const int MOTOR_A_ENA = 32;  // PWM

// Motor B (Right)
const int MOTOR_B_IN3 = 27;
const int MOTOR_B_IN4 = 14;
const int MOTOR_B_ENB = 33;  // PWM

const int PWM_FREQUENCY = 5000;
const int PWM_RESOLUTION = 8;  // 8-bit = 0-255
const int PWM_CHANNEL_A = 0;
const int PWM_CHANNEL_B = 1;

// Motor speed (0-255)
int motorSpeedLeft = 0;
int motorSpeedRight = 0;

// ============================================================
// Spray System State Machine
// ============================================================
enum SprayState {
    SPRAY_IDLE,
    SPRAY_DEPLOY,
    SPRAY_HOLD,
    SPRAY_RETRACT
};

struct SprayController {
    SprayState state;
    unsigned long stateStartTime;
    int servoNum;
    bool active;

    SprayController() : state(SPRAY_IDLE), stateStartTime(0), servoNum(0), active(false) {}

    void start(int num) {
        servoNum = num;
        state = SPRAY_DEPLOY;
        stateStartTime = millis();
        active = true;
    }

    void stop() {
        state = SPRAY_IDLE;
        active = false;
    }

    void update() {
        if (!active) return;

        unsigned long now = millis();

        switch (state) {
            case SPRAY_DEPLOY:
                // Move to spray position (120°)
                if (servoNum == 2 || servoNum == 3) {
                    if (servoNum == 2) servo2.write(120);
                    else servo3.write(120);
                }
                state = SPRAY_HOLD;
                stateStartTime = now;
                break;

            case SPRAY_HOLD:
                // Hold for 800ms
                if (now - stateStartTime >= 800) {
                    state = SPRAY_RETRACT;
                }
                break;

            case SPRAY_RETRACT:
                // Return to 0°
                if (servoNum == 2 || servoNum == 3) {
                    if (servoNum == 2) servo2.write(0);
                    else servo3.write(0);
                }
                state = SPRAY_IDLE;
                active = false;
                break;

            case SPRAY_IDLE:
                active = false;
                break;
        }
    }

    bool isBusy() const {
        return active;
    }
};

SprayController sprayController2;
SprayController sprayController3;
bool autoSprayEnabled = false;
bool lastDetectionTriggered = false;

// ============================================================
// Safety & Watchdog System
// ============================================================
// Watchdog: Stop motors if no WebSocket message for 3 seconds
unsigned long lastWebSocketMessageTime = 0;
const unsigned long WS_WATCHDOG_TIMEOUT = 3000;  // 3 seconds

// PC Offline detection: Disable auto-spray if /detection not called for 10 seconds
unsigned long lastDetectionApiCallTime = 0;
const unsigned long PC_OFFLINE_TIMEOUT = 10000;  // 10 seconds
bool pcOffline = false;
bool previousPcOfflineState = false;

// Spray rate limiting: Minimum 5 seconds between sprays
unsigned long lastSprayTime = 0;
const unsigned long SPRAY_COOLDOWN_MS = 5000;  // 5 seconds

// ============================================================
// Detection Storage
// ============================================================
struct Detection {
    char label[32];      // "fungus" or "pest"
    float confidence;    // 0.0 to 1.0
    unsigned long timestamp;  // millis() when received

    Detection() : confidence(0.0), timestamp(0) {
        label[0] = '\0';
    }

    void set(const char* lbl, float conf) {
        strncpy(label, lbl, sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';
        confidence = conf;
        timestamp = millis();
    }

    bool isValid() const {
        return timestamp > 0 && confidence > 0.0;
    }
};

Detection lastDetection;

// ============================================================
// HTML Dashboard (Stored in PROGMEM)
// ============================================================
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Plant Rover Dashboard</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
            min-height: 100vh;
            color: #eee;
            padding: 20px;
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
        }
        h1 {
            text-align: center;
            color: #4ecdc4;
            margin-bottom: 10px;
            font-size: 2.5rem;
            text-shadow: 0 0 20px rgba(78, 205, 196, 0.5);
        }
        .status {
            text-align: center;
            margin-bottom: 30px;
        }
        .status-indicator {
            display: inline-block;
            width: 12px;
            height: 12px;
            border-radius: 50%;
            background: #ff6b6b;
            margin-right: 8px;
            animation: pulse 1.5s infinite;
        }
        .status-indicator.connected {
            background: #4ecdc4;
        }
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.5; }
        }
        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 20px;
            margin-bottom: 20px;
        }
        .card {
            background: rgba(255, 255, 255, 0.1);
            border-radius: 15px;
            padding: 20px;
            backdrop-filter: blur(10px);
            border: 1px solid rgba(255, 255, 255, 0.1);
        }
        .card h2 {
            color: #4ecdc4;
            margin-bottom: 15px;
            font-size: 1.3rem;
            display: flex;
            align-items: center;
            gap: 10px;
        }
        .joystick-container {
            display: flex;
            justify-content: center;
            align-items: center;
            gap: 30px;
        }
        .joystick {
            width: 180px;
            height: 180px;
            border-radius: 50%;
            background: linear-gradient(145deg, #2d4a6e, #1e3a5f);
            position: relative;
            box-shadow: 0 4px 15px rgba(0, 0, 0, 0.3);
        }
        .joystick-knob {
            width: 60px;
            height: 60px;
            border-radius: 50%;
            background: linear-gradient(145deg, #4ecdc4, #3db8b0);
            position: absolute;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            cursor: pointer;
            box-shadow: 0 4px 10px rgba(0, 0, 0, 0.3);
            transition: box-shadow 0.2s;
        }
        .joystick-knob:active {
            box-shadow: 0 2px 5px rgba(0, 0, 0, 0.3);
        }
        .joystick-label {
            text-align: center;
            margin-top: 10px;
            font-size: 0.9rem;
            color: #888;
        }
        .d-pad {
            display: grid;
            grid-template-areas:
                ". up ."
                "left stop right"
                ". down .";
            gap: 10px;
            max-width: 200px;
            margin: 0 auto;
        }
        .d-btn {
            background: linear-gradient(145deg, #2d4a6e, #1e3a5f);
            border: none;
            border-radius: 10px;
            color: #4ecdc4;
            padding: 20px;
            font-size: 1.5rem;
            cursor: pointer;
            transition: all 0.2s;
            box-shadow: 0 4px 15px rgba(0, 0, 0, 0.3);
        }
        .d-btn:hover {
            background: linear-gradient(145deg, #3d5a7e, #2e4a6f);
            transform: translateY(-2px);
        }
        .d-btn:active {
            transform: translateY(0);
            box-shadow: 0 2px 5px rgba(0, 0, 0, 0.3);
        }
        .d-btn.up { grid-area: up; }
        .d-btn.down { grid-area: down; }
        .d-btn.left { grid-area: left; }
        .d-btn.right { grid-area: right; }
        .d-btn.stop {
            grid-area: stop;
            background: linear-gradient(145deg, #c0392b, #a93226);
        }
        .slider-group {
            margin-bottom: 15px;
        }
        .slider-group label {
            display: block;
            margin-bottom: 5px;
            font-size: 0.9rem;
        }
        .slider-row {
            display: flex;
            align-items: center;
            gap: 15px;
        }
        input[type="range"] {
            flex: 1;
            height: 8px;
            border-radius: 4px;
            background: #1e3a5f;
            outline: none;
            -webkit-appearance: none;
        }
        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            width: 20px;
            height: 20px;
            border-radius: 50%;
            background: #4ecdc4;
            cursor: pointer;
        }
        .value-display {
            background: rgba(78, 205, 196, 0.2);
            padding: 5px 12px;
            border-radius: 5px;
            min-width: 45px;
            text-align: center;
            font-weight: bold;
        }
        .quick-angles {
            display: flex;
            gap: 10px;
            flex-wrap: wrap;
            margin-top: 10px;
        }
        .quick-btn {
            background: rgba(78, 205, 196, 0.2);
            border: 1px solid #4ecdc4;
            color: #4ecdc4;
            padding: 5px 12px;
            border-radius: 5px;
            cursor: pointer;
            transition: all 0.2s;
        }
        .quick-btn:hover {
            background: rgba(78, 205, 196, 0.4);
        }
        .spray-btn {
            background: linear-gradient(145deg, #e74c3c, #c0392b);
            border: none;
            color: white;
            padding: 15px 25px;
            border-radius: 10px;
            font-size: 1rem;
            cursor: pointer;
            transition: all 0.2s;
            box-shadow: 0 4px 15px rgba(0, 0, 0, 0.3);
            width: 100%;
            margin-bottom: 10px;
        }
        .spray-btn:hover {
            background: linear-gradient(145deg, #ff6b5b, #e74c3c);
            transform: translateY(-2px);
        }
        .spray-btn:active {
            transform: translateY(0);
        }
        .spray-btn:disabled {
            background: #666;
            cursor: not-allowed;
            transform: none;
        }
        .auto-spray-toggle {
            display: flex;
            align-items: center;
            justify-content: space-between;
            background: rgba(78, 205, 196, 0.1);
            padding: 12px 15px;
            border-radius: 8px;
            margin-top: 10px;
        }
        .toggle-switch {
            position: relative;
            width: 50px;
            height: 26px;
        }
        .toggle-switch input {
            opacity: 0;
            width: 0;
            height: 0;
        }
        .toggle-slider {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: #ccc;
            transition: .4s;
            border-radius: 26px;
        }
        .toggle-slider:before {
            position: absolute;
            content: "";
            height: 20px;
            width: 20px;
            left: 3px;
            bottom: 3px;
            background-color: white;
            transition: .4s;
            border-radius: 50%;
        }
        input:checked + .toggle-slider {
            background-color: #4ecdc4;
        }
        input:checked + .toggle-slider:before {
            transform: translateX(24px);
        }
        .detection-btns {
            display: flex;
            gap: 10px;
            margin-top: 10px;
        }
        .detect-btn {
            flex: 1;
            padding: 10px;
            border-radius: 8px;
            border: none;
            cursor: pointer;
            font-size: 0.9rem;
            transition: all 0.2s;
        }
        .detect-btn.fungus {
            background: linear-gradient(145deg, #9b59b6, #8e44ad);
            color: white;
        }
        .detect-btn.pest {
            background: linear-gradient(145deg, #f39c12, #e67e22);
            color: white;
        }
        .detect-btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 4px 10px rgba(0, 0, 0, 0.3);
        }
        .detect-btn:disabled {
            background: #666;
            cursor: not-allowed;
            transform: none;
        }
        .log {
            background: rgba(0, 0, 0, 0.3);
            border-radius: 10px;
            padding: 15px;
            max-height: 150px;
            overflow-y: auto;
            font-family: 'Courier New', monospace;
            font-size: 0.8rem;
        }
        .log-entry {
            padding: 3px 0;
            border-bottom: 1px solid rgba(255, 255, 255, 0.1);
        }
        .log-time {
            color: #888;
        }
        .info-panel {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 10px;
        }
        .info-item {
            background: rgba(78, 205, 196, 0.1);
            padding: 10px;
            border-radius: 8px;
        }
        .info-label {
            font-size: 0.8rem;
            color: #888;
        }
        .info-value {
            font-size: 1.1rem;
            color: #4ecdc4;
            font-weight: bold;
        }
        .motor-display {
            display: flex;
            justify-content: space-around;
            padding: 10px;
            background: rgba(0, 0, 0, 0.2);
            border-radius: 8px;
            margin-top: 10px;
        }
        .motor-bar {
            text-align: center;
        }
        .motor-bar-label {
            font-size: 0.8rem;
            margin-bottom: 5px;
        }
        .motor-bar-visual {
            width: 100px;
            height: 10px;
            background: #1e3a5f;
            border-radius: 5px;
            overflow: hidden;
        }
        .motor-bar-fill {
            height: 100%;
            background: linear-gradient(90deg, #4ecdc4, #44a3a0);
            transition: width 0.1s;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🌱 Plant Rover</h1>
        <div class="status">
            <span class="status-indicator" id="statusIndicator"></span>
            <span id="statusText">Connecting...</span>
        </div>

        <div class="grid">
            <!-- Joystick Control -->
            <div class="card">
                <h2>🕹️ Joystick Control</h2>
                <div class="joystick-container">
                    <div class="joystick" id="joystick">
                        <div class="joystick-knob" id="joystickKnob"></div>
                    </div>
                </div>
                <div class="joystick-label">
                    <span id="joystickValue">X: 0, Y: 0</span>
                </div>
                <div class="motor-display">
                    <div class="motor-bar">
                        <div class="motor-bar-label">Left</div>
                        <div class="motor-bar-visual">
                            <div class="motor-bar-fill" id="leftMotorBar" style="width: 0%"></div>
                        </div>
                        <div class="motor-bar-label" id="leftMotorVal">0</div>
                    </div>
                    <div class="motor-bar">
                        <div class="motor-bar-label">Right</div>
                        <div class="motor-bar-visual">
                            <div class="motor-bar-fill" id="rightMotorBar" style="width: 0%"></div>
                        </div>
                        <div class="motor-bar-label" id="rightMotorVal">0</div>
                    </div>
                </div>
            </div>

            <!-- D-Pad Control -->
            <div class="card">
                <h2>🚗 Direction Control</h2>
                <div class="d-pad">
                    <button class="d-btn up" onmousedown="sendMotor('F')" onmouseup="stopMotor()" onmouseleave="stopMotor()" ontouchstart="sendMotor('F')" ontouchend="stopMotor()">↑</button>
                    <button class="d-btn left" onmousedown="sendMotor('L')" onmouseup="stopMotor()" onmouseleave="stopMotor()" ontouchstart="sendMotor('L')" ontouchend="stopMotor()">←</button>
                    <button class="d-btn stop" onmousedown="stopMotor()" ontouchstart="stopMotor()">⬛</button>
                    <button class="d-btn right" onmousedown="sendMotor('R')" onmouseup="stopMotor()" onmouseleave="stopMotor()" ontouchstart="sendMotor('R')" ontouchend="stopMotor()">→</button>
                    <button class="d-btn down" onmousedown="sendMotor('B')" onmouseup="stopMotor()" onmouseleave="stopMotor()" ontouchstart="sendMotor('B')" ontouchend="stopMotor()">↓</button>
                </div>
            </div>

            <!-- Servo 1 -->
            <div class="card">
                <h2>🔧 Servo 1 (GPIO 18)</h2>
                <div class="slider-group">
                    <div class="slider-row">
                        <input type="range" id="servo1" min="0" max="180" value="90" oninput="updateServo(1, this.value)">
                        <span class="value-display" id="servo1Val">90°</span>
                    </div>
                </div>
                <div class="quick-angles">
                    <button class="quick-btn" onclick="setServo(1, 0)">0°</button>
                    <button class="quick-btn" onclick="setServo(1, 45)">45°</button>
                    <button class="quick-btn" onclick="setServo(1, 90)">90°</button>
                    <button class="quick-btn" onclick="setServo(1, 135)">135°</button>
                    <button class="quick-btn" onclick="setServo(1, 180)">180°</button>
                </div>
            </div>

            <!-- Spray Control -->
            <div class="card">
                <h2>💨 Spray Control</h2>
                <button class="spray-btn" id="spray2Btn" onclick="triggerSpray(2)">Spray Nozzle 2</button>
                <button class="spray-btn" id="spray3Btn" onclick="triggerSpray(3)">Spray Nozzle 3</button>
                <button class="spray-btn" id="sprayBothBtn" onclick="triggerSpray('both')">Spray Both</button>

                <div class="auto-spray-toggle">
                    <span>Auto Spray on Detection</span>
                    <label class="toggle-switch">
                        <input type="checkbox" id="autoSprayToggle" onchange="toggleAutoSpray()">
                        <span class="toggle-slider"></span>
                    </label>
                </div>

                <div class="detection-btns">
                    <button class="detect-btn fungus" id="fungusBtn" onclick="sendDetection('fungus')">
                        🍄 Sim Fungus
                    </button>
                    <button class="detect-btn pest" id="pestBtn" onclick="sendDetection('pest')">
                        🐛 Sim Pest
                    </button>
                </div>
            </div>

            <!-- Info Panel -->
            <div class="card">
                <h2>📊 Rover Info</h2>
                <div class="info-panel">
                    <div class="info-item">
                        <div class="info-label">IP Address</div>
                        <div class="info-value">192.168.4.1</div>
                    </div>
                    <div class="info-item">
                        <div class="info-label">WiFi SSID</div>
                        <div class="info-value">PlantRover</div>
                    </div>
                    <div class="info-item">
                        <div class="info-label">HTTP Port</div>
                        <div class="info-value">80</div>
                    </div>
                    <div class="info-item">
                        <div class="info-label">WS Port</div>
                        <div class="info-value">81</div>
                    </div>
                    <div class="info-item" style="grid-column: 1 / -1;">
                        <div class="info-label">Auto Spray</div>
                        <div class="info-value" id="autoSprayStatus">Disabled</div>
                    </div>
                </div>
            </div>

            <!-- JSON Commands Reference -->
            <div class="card" style="grid-column: 1 / -1;">
                <h2>📝 JSON Commands Reference</h2>
                <pre style="background: rgba(0,0,0,0.3); padding: 15px; border-radius: 8px; overflow-x: auto; font-size: 0.85rem;">
// Differential Drive (x: -255 to 255, y: -255 to 255)
{"type":"drive","x":-100,"y":200}

// Servo 1 Control
{"type":"servo1","angle":90}

// Spray Commands
{"type":"spray","id":2}           // Spray nozzle 2
{"type":"spray","id":3}           // Spray nozzle 3
{"type":"spray","id":"both"}      // Spray both (staggered)

// Auto Spray Toggle
{"type":"autoSpray","enabled":true}

// Detection (triggers auto-spray if enabled)
{"type":"detection","value":"fungus"}
{"type":"detection","value":"pest"}</pre>
            </div>

            <!-- Event Log -->
            <div class="card" style="grid-column: 1 / -1;">
                <h2>📋 Event Log</h2>
                <div class="log" id="eventLog">
                    <div class="log-entry"><span class="log-time">[System]</span> Dashboard loaded</div>
                </div>
            </div>
        </div>
    </div>

    <script>
        let ws;
        let reconnectInterval;
        let joystickActive = false;
        let joystickInterval;

        function connectWebSocket() {
            ws = new WebSocket('ws://' + window.location.hostname + ':81/');

            ws.onopen = function() {
                document.getElementById('statusIndicator').classList.add('connected');
                document.getElementById('statusText').textContent = 'Connected';
                addLog('WebSocket connected');
                clearInterval(reconnectInterval);
            };

            ws.onclose = function() {
                document.getElementById('statusIndicator').classList.remove('connected');
                document.getElementById('statusText').textContent = 'Disconnected - Reconnecting...';
                addLog('WebSocket disconnected');
                reconnectInterval = setInterval(connectWebSocket, 3000);
            };

            ws.onerror = function(error) {
                addLog('WebSocket error');
            };

            ws.onmessage = function(event) {
                const data = event.data;
                addLog('Received: ' + data);
            };
        }

        function sendJSON(obj) {
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send(JSON.stringify(obj));
                return true;
            }
            return false;
        }

        function sendCommand(cmd) {
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send(cmd);
                return true;
            }
            return false;
        }

        // Joystick Control
        const joystick = document.getElementById('joystick');
        const joystickKnob = document.getElementById('joystickKnob');

        function updateJoystick(clientX, clientY) {
            const rect = joystick.getBoundingClientRect();
            const centerX = rect.left + rect.width / 2;
            const centerY = rect.top + rect.height / 2;

            let dx = clientX - centerX;
            let dy = clientY - centerY;

            const maxDistance = rect.width / 2;
            const distance = Math.sqrt(dx * dx + dy * dy);

            if (distance > maxDistance) {
                dx = dx / distance * maxDistance;
                dy = dy / distance * maxDistance;
            }

            joystickKnob.style.transform = `translate(calc(-50% + ${dx}px), calc(-50% + ${dy}px))`;

            // Map to -255 to 255 range
            const x = Math.round((dx / maxDistance) * 255);
            const y = Math.round((-dy / maxDistance) * 255); // Invert Y

            document.getElementById('joystickValue').textContent = `X: ${x}, Y: ${y}`;

            sendJSON({ type: 'drive', x: x, y: y });
        }

        joystick.addEventListener('mousedown', (e) => {
            joystickActive = true;
            updateJoystick(e.clientX, e.clientY);
        });

        joystick.addEventListener('touchstart', (e) => {
            e.preventDefault();
            joystickActive = true;
            updateJoystick(e.touches[0].clientX, e.touches[0].clientY);
        });

        document.addEventListener('mousemove', (e) => {
            if (joystickActive) {
                updateJoystick(e.clientX, e.clientY);
            }
        });

        document.addEventListener('touchmove', (e) => {
            if (joystickActive) {
                updateJoystick(e.touches[0].clientX, e.touches[0].clientY);
            }
        });

        document.addEventListener('mouseup', () => {
            if (joystickActive) {
                joystickActive = false;
                joystickKnob.style.transform = 'translate(-50%, -50%)';
                document.getElementById('joystickValue').textContent = 'X: 0, Y: 0';
                sendJSON({ type: 'drive', x: 0, y: 0 });
            }
        });

        document.addEventListener('touchend', () => {
            if (joystickActive) {
                joystickActive = false;
                joystickKnob.style.transform = 'translate(-50%, -50%)';
                document.getElementById('joystickValue').textContent = 'X: 0, Y: 0';
                sendJSON({ type: 'drive', x: 0, y: 0 });
            }
        });

        // Legacy Motor Controls
        function sendMotor(dir) {
            sendCommand('M:' + dir);
            addLog('Motor: ' + dir);
        }

        function stopMotor() {
            sendCommand('M:S');
            addLog('Motor: STOP');
        }

        // Servo Control
        function updateServo(num, angle) {
            document.getElementById('servo' + num + 'Val').textContent = angle + '°';
            sendJSON({ type: 'servo' + num, angle: parseInt(angle) });
        }

        function setServo(num, angle) {
            document.getElementById('servo' + num).value = angle;
            updateServo(num, angle);
            addLog('Servo ' + num + ': ' + angle + '°');
        }

        // Spray Control
        function triggerSpray(id) {
            sendJSON({ type: 'spray', id: id });
            addLog('Spray triggered: ' + id);
        }

        function toggleAutoSpray() {
            const enabled = document.getElementById('autoSprayToggle').checked;
            sendJSON({ type: 'autoSpray', enabled: enabled });
            document.getElementById('autoSprayStatus').textContent = enabled ? 'Enabled' : 'Disabled';
            addLog('Auto spray: ' + (enabled ? 'Enabled' : 'Disabled'));
        }

        function sendDetection(type) {
            sendJSON({ type: 'detection', value: type });
            addLog('Detection sent: ' + type);
        }

        function updateMotorBars(left, right) {
            const leftPercent = Math.abs(left / 255 * 100);
            const rightPercent = Math.abs(right / 255 * 100);
            document.getElementById('leftMotorBar').style.width = leftPercent + '%';
            document.getElementById('rightMotorBar').style.width = rightPercent + '%';
            document.getElementById('leftMotorVal').textContent = left;
            document.getElementById('rightMotorVal').textContent = right;
        }

        function addLog(msg) {
            const log = document.getElementById('eventLog');
            const time = new Date().toLocaleTimeString();
            const entry = document.createElement('div');
            entry.className = 'log-entry';
            entry.innerHTML = '<span class="log-time">[' + time + ']</span> ' + msg;
            log.insertBefore(entry, log.firstChild);
            while (log.children.length > 50) {
                log.removeChild(log.lastChild);
            }
        }

        // Initialize
        connectWebSocket();

        // Send initial positions
        setTimeout(() => {
            sendJSON({ type: 'servo1', angle: 90 });
            sendJSON({ type: 'drive', x: 0, y: 0 });
        }, 1000);
    </script>
</body>
</html>
)rawliteral";

// ============================================================
// Motor Control Functions
// ============================================================
void initMotors() {
    // Direction pins
    pinMode(MOTOR_A_IN1, OUTPUT);
    pinMode(MOTOR_A_IN2, OUTPUT);
    pinMode(MOTOR_B_IN3, OUTPUT);
    pinMode(MOTOR_B_IN4, OUTPUT);

    // PWM setup
    ledcSetup(PWM_CHANNEL_A, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcSetup(PWM_CHANNEL_B, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcAttachPin(MOTOR_A_ENA, PWM_CHANNEL_A);
    ledcAttachPin(MOTOR_B_ENB, PWM_CHANNEL_B);

    // Initial state - stopped
    stopMotors();
}

void stopMotors() {
    digitalWrite(MOTOR_A_IN1, LOW);
    digitalWrite(MOTOR_A_IN2, LOW);
    digitalWrite(MOTOR_B_IN3, LOW);
    digitalWrite(MOTOR_B_IN4, LOW);
    ledcWrite(PWM_CHANNEL_A, 0);
    ledcWrite(PWM_CHANNEL_B, 0);
    motorSpeedLeft = 0;
    motorSpeedRight = 0;
}

void setMotor(int pwmChannel, int in1Pin, int in2Pin, int speed) {
    if (speed > 0) {
        digitalWrite(in1Pin, HIGH);
        digitalWrite(in2Pin, LOW);
        ledcWrite(pwmChannel, speed);
    } else if (speed < 0) {
        digitalWrite(in1Pin, LOW);
        digitalWrite(in2Pin, HIGH);
        ledcWrite(pwmChannel, -speed);
    } else {
        digitalWrite(in1Pin, LOW);
        digitalWrite(in2Pin, LOW);
        ledcWrite(pwmChannel, 0);
    }
}

void setMotorLeft(int speed) {
    speed = constrain(speed, -255, 255);
    motorSpeedLeft = speed;
    setMotor(PWM_CHANNEL_A, MOTOR_A_IN1, MOTOR_A_IN2, speed);
}

void setMotorRight(int speed) {
    speed = constrain(speed, -255, 255);
    motorSpeedRight = speed;
    setMotor(PWM_CHANNEL_B, MOTOR_B_IN3, MOTOR_B_IN4, speed);
}

// Differential Steering: x=steer (-255 to 255), y=throttle (-255 to 255)
// Positive y = forward, Negative y = backward
// Positive x = right, Negative x = left
void differentialDrive(int x, int y) {
    x = constrain(x, -255, 255);
    y = constrain(y, -255, 255);

    // Differential steering algorithm
    // Left motor = throttle - steering
    // Right motor = throttle + steering
    int leftSpeed = y - x;
    int rightSpeed = y + x;

    // Constrain to valid PWM range
    leftSpeed = constrain(leftSpeed, -255, 255);
    rightSpeed = constrain(rightSpeed, -255, 255);

    setMotorLeft(leftSpeed);
    setMotorRight(rightSpeed);

    Serial.printf("DiffDrive - X:%d Y:%d -> L:%d R:%d\n", x, y, leftSpeed, rightSpeed);
}

// Direction: F=Forward, B=Backward, L=Left, R=Right, S=Stop
void driveMotor(char direction) {
    switch (direction) {
        case 'F':  // Forward
            setMotorLeft(150);
            setMotorRight(150);
            break;
        case 'B':  // Backward
            setMotorLeft(-150);
            setMotorRight(-150);
            break;
        case 'L':  // Left (pivot)
            setMotorLeft(-150);
            setMotorRight(150);
            break;
        case 'R':  // Right (pivot)
            setMotorLeft(150);
            setMotorRight(-150);
            break;
        case 'S':  // Stop
        default:
            stopMotors();
            break;
    }
}

// ============================================================
// Servo Functions
// ============================================================
void initServos() {
    servo1.attach(SERVO1_PIN);
    servo2.attach(SERVO2_PIN);
    servo3.attach(SERVO3_PIN);

    // Set initial positions
    servo1.write(servo1Angle);
    servo2.write(servo2Angle);
    servo3.write(servo3Angle);

    delay(500);
}

void setServoAngle(uint8_t servoNum, uint8_t angle) {
    angle = constrain(angle, 0, 180);
    switch (servoNum) {
        case 1:
            servo1Angle = angle;
            servo1.write(angle);
            break;
        case 2:
            servo2Angle = angle;
            servo2.write(angle);
            break;
        case 3:
            servo3Angle = angle;
            servo3.write(angle);
            break;
    }
    Serial.printf("Servo %d set to %d\n", servoNum, angle);
}

// ============================================================
// Spray Functions
// ============================================================
void triggerSpray(int id) {
    unsigned long now = millis();

    // Rate limiting check
    if (now - lastSprayTime < SPRAY_COOLDOWN_MS) {
        unsigned long remaining = SPRAY_COOLDOWN_MS - (now - lastSprayTime);
        Serial.printf("Spray on cooldown: %lu ms remaining\n", remaining);
        return;
    }

    if (id == 2) {
        if (!sprayController2.isBusy()) {
            sprayController2.start(2);
            lastSprayTime = now;
            Serial.println("Spray 2 triggered");
        } else {
            Serial.println("Spray 2 already busy");
        }
    } else if (id == 3) {
        if (!sprayController3.isBusy()) {
            sprayController3.start(3);
            lastSprayTime = now;
            Serial.println("Spray 3 triggered");
        } else {
            Serial.println("Spray 3 already busy");
        }
    }
}

void triggerSprayBoth() {
    unsigned long now = millis();

    // Rate limiting check
    if (now - lastSprayTime < SPRAY_COOLDOWN_MS) {
        unsigned long remaining = SPRAY_COOLDOWN_MS - (now - lastSprayTime);
        Serial.printf("Spray on cooldown: %lu ms remaining\n", remaining);
        return;
    }

    if (!sprayController2.isBusy() && !sprayController3.isBusy()) {
        sprayController2.start(2);
        // Stagger spray 3 by 200ms
        delay(200);
        sprayController3.start(3);
        lastSprayTime = millis();
        Serial.println("Both sprays triggered (staggered)");
    } else {
        Serial.println("Sprays busy - cannot trigger both");
    }
}

// Non-blocking staggered spray both (for use in main loop)
unsigned long sprayBothStartTime = 0;
bool sprayBothPending = false;

void triggerSprayBothNonBlocking() {
    unsigned long now = millis();

    // Rate limiting check
    if (now - lastSprayTime < SPRAY_COOLDOWN_MS) {
        unsigned long remaining = SPRAY_COOLDOWN_MS - (now - lastSprayTime);
        Serial.printf("Spray on cooldown: %lu ms remaining\n", remaining);
        return;
    }

    if (!sprayController2.isBusy() && !sprayController3.isBusy()) {
        sprayController2.start(2);
        sprayBothStartTime = now;
        sprayBothPending = true;
        lastSprayTime = now;  // Update cooldown time
        Serial.println("Both sprays triggered (staggered NB)");
    }
}

void updateSpraySystem() {
    sprayController2.update();
    sprayController3.update();

    // Handle staggered spray
    if (sprayBothPending && (millis() - sprayBothStartTime >= 200)) {
        sprayController3.start(3);
        sprayBothPending = false;
    }
}

// ============================================================
// JSON Command Processor
// ============================================================
void processJSONCommand(JsonDocument& doc) {
    const char* type = doc["type"];

    if (strcmp(type, "drive") == 0) {
        // Differential drive with x/y
        int x = doc["x"];
        int y = doc["y"];
        differentialDrive(x, y);
    }
    else if (strcmp(type, "servo1") == 0) {
        int angle = doc["angle"];
        setServoAngle(1, angle);
    }
    else if (strcmp(type, "spray") == 0) {
        // Check if id is number or string
        if (doc["id"].is<int>()) {
            int id = doc["id"];
            triggerSpray(id);
        } else if (doc["id"].is<const char*>()) {
            const char* idStr = doc["id"];
            if (strcmp(idStr, "both") == 0) {
                triggerSprayBothNonBlocking();
            }
        }
    }
    else if (strcmp(type, "autoSpray") == 0) {
        bool enabled = doc["enabled"];
        autoSprayEnabled = enabled;
        Serial.printf("Auto spray %s\n", enabled ? "enabled" : "disabled");
    }
    else if (strcmp(type, "detection") == 0) {
        const char* value = doc["value"];
        Serial.printf("Detection: %s\n", value);

        // Trigger auto-spray if enabled and detection is fungus or pest
        if (autoSprayEnabled &&
            (strcmp(value, "fungus") == 0 || strcmp(value, "pest") == 0)) {
            Serial.println("Auto-spray triggered by detection!");
            triggerSprayBothNonBlocking();
        }
    }
}

// ============================================================
// Command Processor (Legacy + JSON)
// ============================================================
void processCommand(char* command) {
    // Try to parse as JSON first
    // Check if it looks like JSON (starts with '{')
    if (command[0] == '{') {
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, command);

        if (!error) {
            processJSONCommand(doc);
            return;
        } else {
            Serial.printf("JSON parse error: %s\n", error.c_str());
        }
    }

    // Legacy commands
    // Motor commands: M:F, M:B, M:L, M:R, M:S
    if (strncmp(command, "M:", 2) == 0) {
        char dir = command[2];
        driveMotor(dir);
    }
    // Motor speed: SD:left,right (e.g., "SD:200,200")
    else if (strncmp(command, "SD:", 3) == 0) {
        int left, right;
        if (sscanf(command + 3, "%d,%d", &left, &right) == 2) {
            setMotorLeft(left);
            setMotorRight(right);
        }
    }
    // Servo commands: S1:angle, S2:angle, S3:angle (e.g., "S1:90")
    else if (strncmp(command, "S1:", 3) == 0) {
        uint8_t angle = atoi(command + 3);
        setServoAngle(1, angle);
    }
    else if (strncmp(command, "S2:", 3) == 0) {
        uint8_t angle = atoi(command + 3);
        setServoAngle(2, angle);
    }
    else if (strncmp(command, "S3:", 3) == 0) {
        uint8_t angle = atoi(command + 3);
        setServoAngle(3, angle);
    }
}

// ============================================================
// WebSocket Event Handler
// ============================================================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected!\n", num);
            break;

        case WStype_CONNECTED:
            Serial.printf("[%u] Connected!\n", num);
            webSocket.sendTXT(num, "Connected to Plant Rover");
            // Reset watchdog on new connection
            lastWebSocketMessageTime = millis();
            break;

        case WStype_TEXT:
            Serial.printf("[%u] Received: %s\n", num, payload);
            // Update watchdog timer on any message
            lastWebSocketMessageTime = millis();
            processCommand((char*)payload);
            break;

        case WStype_BIN:
            // Update watchdog timer on binary messages too
            lastWebSocketMessageTime = millis();
            break;

        case WStype_ERROR:
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
            break;
    }
}

// ============================================================
// HTTP Server Handlers
// ============================================================
void handleRoot() {
    httpServer.send(200, "text/html", htmlPage);
}

void handleNotFound() {
    httpServer.send(404, "text/plain", "404: Not found");
}

void handleStatus() {
    String json = "{";
    json += "\"connected\":true,";
    json += "\"servo1\":" + String(servo1Angle) + ",";
    json += "\"servo2\":" + String(servo2Angle) + ",";
    json += "\"servo3\":" + String(servo3Angle) + ",";
    json += "\"motorLeft\":" + String(motorSpeedLeft) + ",";
    json += "\"motorRight\":" + String(motorSpeedRight) + ",";
    json += "\"autoSprayEnabled\":" + String(autoSprayEnabled ? "true" : "false") + ",";
    json += "\"spray2Busy\":" + String(sprayController2.isBusy() ? "true" : "false") + ",";
    json += "\"spray3Busy\":" + String(sprayController3.isBusy() ? "true" : "false") + ",";
    json += "\"ip\":\"" + WiFi.softAPIP().toString() + "\"";
    json += "}";
    httpServer.send(200, "application/json", json);
}

// ============================================================
// HTTP POST /detection Handler
// ============================================================
void handleDetection() {
    // Update PC heartbeat - detection server is alive
    lastDetectionApiCallTime = millis();

    // Only accept POST requests
    if (httpServer.method() != HTTP_POST) {
        httpServer.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
        return;
    }

    // Check if body exists
    if (!httpServer.hasArg("plain")) {
        httpServer.send(400, "application/json", "{\"error\":\"No body\"}");
        return;
    }

    String body = httpServer.arg("plain");

    // Parse JSON
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        Serial.printf("JSON parse error in /detection: %s\n", error.c_str());
        httpServer.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    // Extract fields
    const char* label = doc["label"];
    float confidence = doc["confidence"];

    // Validate required fields
    if (label == nullptr || doc["confidence"].isNull()) {
        httpServer.send(400, "application/json", "{\"error\":\"Missing label or confidence\"}");
        return;
    }

    confidence = constrain(confidence, 0.0f, 1.0f);
    Serial.printf("Detection received: label='%s', confidence=%.2f\n", label, confidence);

    // Check confidence threshold
    if (confidence > 0.70f) {
        // Store in global detection struct
        lastDetection.set(label, confidence);
        Serial.printf("High confidence detection stored! %.2f > 0.70\n", confidence);

        // Broadcast to all WebSocket clients
        StaticJsonDocument<256> broadcastDoc;
        broadcastDoc["type"] = "detection";
        broadcastDoc["label"] = label;
        broadcastDoc["confidence"] = confidence;

        String broadcastJson;
        serializeJson(broadcastDoc, broadcastJson);
        webSocket.broadcastTXT(broadcastJson);
        Serial.printf("Broadcasted to WebSocket clients: %s\n", broadcastJson.c_str());

        // Trigger auto-spray if enabled AND PC is online
        if (autoSprayEnabled && !pcOffline) {
            Serial.println("Auto-spray enabled - triggering spray!");
            triggerSprayBothNonBlocking();
        } else if (pcOffline) {
            Serial.println("PC offline - auto-spray disabled, not triggering");
        }
    }

    // Send 200 OK response
    httpServer.send(200, "application/json", "{\"received\":true}");
}

// ============================================================
// Setup & Loop
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=================================");
    Serial.println("🌱 Plant Rover Starting...");
    Serial.println("=================================\n");

    // Initialize Servos
    Serial.println("Initializing servos...");
    initServos();
    Serial.println("Servos ready on GPIO 18, 19, 21");

    // Initialize Motors
    Serial.println("Initializing motor driver...");
    initMotors();
    Serial.println("Motors ready");

    // Setup WiFi Access Point
    Serial.println("\nSetting up WiFi Access Point...");
    WiFi.softAPConfig(localIP, gateway, subnet);
    WiFi.softAP(AP_SSID, AP_PASSWORD);

    Serial.println("WiFi AP Started!");
    Serial.printf("SSID: %s\n", AP_SSID);
    Serial.printf("Password: %s\n", AP_PASSWORD);
    Serial.printf("IP Address: %s\n", WiFi.softAPIP().toString().c_str());

    // Setup HTTP Server
    Serial.println("\nStarting HTTP server...");
    httpServer.on("/", handleRoot);
    httpServer.on("/status", handleStatus);
    httpServer.on("/detection", handleDetection);
    httpServer.onNotFound(handleNotFound);
    httpServer.begin();
    Serial.printf("HTTP server running on port %d\n", HTTP_PORT);
    Serial.println("Endpoints: /, /status, /detection (POST)");

    // Setup WebSocket Server
    Serial.println("Starting WebSocket server...");
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    Serial.printf("WebSocket server running on port %d\n", WS_PORT);

    Serial.println("\n=================================");
    Serial.println("🌱 Plant Rover Ready!");
    Serial.println("=================================\n");
    Serial.println("Connect to: http://192.168.4.1");
    Serial.println("WebSocket: ws://192.168.4.1:81\n");
}

void loop() {
    webSocket.loop();
    httpServer.handleClient();
    updateSpraySystem();  // Non-blocking spray state machine

    unsigned long now = millis();

    // ============================================================
    // Watchdog: Stop motors if no WebSocket message for 3 seconds
    // ============================================================
    if (lastWebSocketMessageTime > 0) {
        if (now - lastWebSocketMessageTime > WS_WATCHDOG_TIMEOUT) {
            // Watchdog triggered - stop all motors
            if (motorSpeedLeft != 0 || motorSpeedRight != 0) {
                Serial.println("Watchdog: No WS message for 3 seconds, stopping motors");
                stopMotors();
            }
        }
    }

    // ============================================================
    // PC Offline Detection: Disable auto-spray if /detection not called for 10 seconds
    // ============================================================
    if (lastDetectionApiCallTime > 0) {
        bool wasOffline = pcOffline;
        pcOffline = (now - lastDetectionApiCallTime > PC_OFFLINE_TIMEOUT);

        // State changed - broadcast to clients
        if (pcOffline != wasOffline || pcOffline != previousPcOfflineState) {
            if (pcOffline) {
                Serial.println("PC Offline detected - disabling auto-spray");
                // Force disable auto-spray
                autoSprayEnabled = false;

                // Broadcast PC offline event to all clients
                StaticJsonDocument<128> eventDoc;
                eventDoc["type"] = "pcOffline";
                eventDoc["offline"] = true;
                String eventJson;
                serializeJson(eventDoc, eventJson);
                webSocket.broadcastTXT(eventJson);
            } else {
                Serial.println("PC Online detected");
                // Broadcast PC online event
                StaticJsonDocument<128> eventDoc;
                eventDoc["type"] = "pcOffline";
                eventDoc["offline"] = false;
                String eventJson;
                serializeJson(eventDoc, eventJson);
                webSocket.broadcastTXT(eventJson);
            }
            previousPcOfflineState = pcOffline;
        }
    }

    delay(2);
}
