/**
 * Plant Rover - ESP32 WiFi Controlled Robot
 *
 * Arduino IDE Version
 *
 * Hardware:
 * - ESP32 WROOM
 * - 1x Servo (spray aim) on GPIO 5
 * - L298N #1 (Drive):
 *   - IN1: GPIO 23, IN2: GPIO 13 (Motor A - Left)
 *   - IN3: GPIO 27, IN4: GPIO 14 (Motor B - Right)
 *   - ENA: GPIO 33 (PWM Left)
 *   - ENB: GPIO 32 (PWM Right)
 * - L298N #2 (Spray BO Motors - single direction):
 *   - IN1: GPIO 18, IN2: GPIO 5  (Spray Motor A - Left)
 *   - IN3: GPIO 21, IN4: GPIO 22 (Spray Motor B - Right)
 *   - ENA: GPIO 4  (PWM Left)
 *   - ENB: GPIO 15 (PWM Right)
 *
 * Features:
 * - WiFi AP: "PlantRover" / "rover1234"
 * - HTTP Server on port 80 (HTML Dashboard)
 * - WebSocket Server on port 81
 * - Differential steering
 * - Spray aim servo (0-180) + BO motor spray activation
 * - Watchdog and PC offline detection
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
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
// Servo + Spray System Configuration
// ============================================================
// Single servo: spray assembly aim rotation
const int SPRAY_AIM_PIN = 5;  // 0=left, 180=home, 360=right

int sprayAimAngle = 180;  // 0=full left, 180=home/center, 360=full right

// Servo driven directly via LEDC PWM (50Hz, 16-bit)
// No Servo library needed -- more reliable on ESP32
#define SERVO_FREQ 50        // 50Hz = 20ms period
#define SERVO_RES 16         // 16-bit resolution
#define SERVO_MIN_US 500     // minimum pulse width (0 deg)
#define SERVO_MAX_US 2500    // maximum pulse width (360 deg)

// Spray BO Motor Driver (L298N #2 - dedicated to spray activation)
// Single-direction: IN2/IN4 hardwired to GND on the L298N board
const int SPRAY_IN1 = 18;  // Spray Motor A (left)
const int SPRAY_ENA = 4;   // Spray Motor A (left) PWM speed
const int SPRAY_IN3 = 21;  // Spray Motor B (right)
const int SPRAY_ENB = 15;  // Spray Motor B (right) PWM speed
const int SPRAY_MOTOR_SPEED = 255;  // Full speed for BO motors

// ============================================================
// Motor Driver (L298N #1 - Drive) Configuration
// ============================================================
// Motor A (Left)
const int MOTOR_A_IN1 = 23;   // Left motor direction A
const int MOTOR_A_IN2 = 13;   // Left motor direction B
const int MOTOR_A_ENA = 33;   // Left PWM

// Motor B (Right)
const int MOTOR_B_IN3 = 27;
const int MOTOR_B_IN4 = 14;
const int MOTOR_B_ENB = 32;   // Right PWM

const int PWM_FREQUENCY = 5000;
const int PWM_RESOLUTION = 8;  // 8-bit = 0-255

// LEDC channels (old API uses channel numbers, not pins)
const int PWM_CH_DRIVE_A = 0;   // Motor A (left) PWM
const int PWM_CH_DRIVE_B = 1;   // Motor B (right) PWM
const int PWM_CH_SPRAY_A = 2;   // Spray motor A PWM
const int PWM_CH_SPRAY_B = 3;   // Spray motor B PWM
const int PWM_CH_SERVO   = 4;   // Servo PWM

int motorSpeedLeft = 0;
int motorSpeedRight = 0;

// ============================================================
// Safety & Watchdog System
// ============================================================
unsigned long lastWebSocketMessageTime = 0;
const unsigned long WS_WATCHDOG_TIMEOUT = 3000;  // 3 seconds

unsigned long lastDetectionApiCallTime = 0;
const unsigned long PC_OFFLINE_TIMEOUT = 10000;  // 10 seconds
bool pcOffline = false;
bool previousPcOfflineState = false;

unsigned long lastSprayTime = 0;
const unsigned long SPRAY_COOLDOWN_MS = 5000;  // 5 seconds
const unsigned long SPRAY_HOLD_TIME_MS = 800;  // BO motor run duration (ms)

// ============================================================
// Spray System State Machine (BO Motor based)
// ============================================================
enum SprayMotorState {
    SPRAY_MOTOR_IDLE,
    SPRAY_MOTOR_RUNNING
};

struct SprayMotorController {
    SprayMotorState state;
    unsigned long startTime;
    int motorId;  // 1=left, 2=right

    SprayMotorController() : state(SPRAY_MOTOR_IDLE), startTime(0), motorId(0) {}

    void start(int id) {
        motorId = id;
        state = SPRAY_MOTOR_RUNNING;
        startTime = millis();

        if (motorId == 1) {
            digitalWrite(SPRAY_IN1, HIGH);
            ledcWrite(PWM_CH_SPRAY_A, SPRAY_MOTOR_SPEED);
        } else {
            digitalWrite(SPRAY_IN3, HIGH);
            ledcWrite(PWM_CH_SPRAY_B, SPRAY_MOTOR_SPEED);
        }
    }

    void update() {
        if (state != SPRAY_MOTOR_RUNNING) return;
        if (millis() - startTime >= SPRAY_HOLD_TIME_MS) {
            stop();
        }
    }

    void stop() {
        if (motorId == 1) {
            digitalWrite(SPRAY_IN1, LOW);
            ledcWrite(PWM_CH_SPRAY_A, 0);
        } else if (motorId == 2) {
            digitalWrite(SPRAY_IN3, LOW);
            ledcWrite(PWM_CH_SPRAY_B, 0);
        }
        state = SPRAY_MOTOR_IDLE;
    }

    bool isBusy() const { return state != SPRAY_MOTOR_IDLE; }
};

SprayMotorController sprayMotorA;  // Left spray BO motor
SprayMotorController sprayMotorB;  // Right spray BO motor
bool autoSprayEnabled = false;

// ============================================================
// Detection Storage
// ============================================================
struct Detection {
    char label[32];
    float confidence;
    unsigned long timestamp;

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
// Motor Control Functions
// ============================================================
void initMotors() {
    pinMode(MOTOR_A_IN1, OUTPUT);
    pinMode(MOTOR_A_IN2, OUTPUT);
    pinMode(MOTOR_B_IN3, OUTPUT);
    pinMode(MOTOR_B_IN4, OUTPUT);

    // LEDC PWM setup (old API)
    ledcSetup(PWM_CH_DRIVE_A, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcSetup(PWM_CH_DRIVE_B, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcAttachPin(MOTOR_A_ENA, PWM_CH_DRIVE_A);
    ledcAttachPin(MOTOR_B_ENB, PWM_CH_DRIVE_B);

    stopMotors();
}

void stopMotors() {
    // Set all direction pins LOW (brake/coast mode)
    digitalWrite(MOTOR_A_IN1, LOW);
    digitalWrite(MOTOR_A_IN2, LOW);
    digitalWrite(MOTOR_B_IN3, LOW);
    digitalWrite(MOTOR_B_IN4, LOW);

    // Set PWM to 0 (important - prevents ghost power)
    ledcWrite(PWM_CH_DRIVE_A, 0);
    ledcWrite(PWM_CH_DRIVE_B, 0);

    motorSpeedLeft = 0;
    motorSpeedRight = 0;
}

void setMotor(int pwmChannel, int in1Pin, int in2Pin, int speed) {
    // Minimum PWM threshold - below this, motor won't move reliably
    const int MIN_PWM = 80;

    if (speed > 0) {
        // Forward
        digitalWrite(in1Pin, HIGH);
        digitalWrite(in2Pin, LOW);
        // Apply minimum threshold for actual movement
        int pwmValue = (speed < MIN_PWM) ? 0 : speed;
        ledcWrite(pwmChannel, pwmValue);
    } else if (speed < 0) {
        // Reverse
        digitalWrite(in1Pin, LOW);
        digitalWrite(in2Pin, HIGH);
        // Apply minimum threshold for actual movement
        int pwmValue = (-speed < MIN_PWM) ? 0 : -speed;
        ledcWrite(pwmChannel, pwmValue);
    } else {
        // Stop
        digitalWrite(in1Pin, LOW);
        digitalWrite(in2Pin, LOW);
        ledcWrite(pwmChannel, 0);
    }
}

void setMotorLeft(int speed) {
    speed = constrain(speed, -255, 255);
    motorSpeedLeft = speed;
    setMotor(PWM_CH_DRIVE_A, MOTOR_A_IN1, MOTOR_A_IN2, speed);
}

void setMotorRight(int speed) {
    speed = constrain(speed, -255, 255);
    motorSpeedRight = speed;
    setMotor(PWM_CH_DRIVE_B, MOTOR_B_IN3, MOTOR_B_IN4, speed);
}

// Differential steering: x=steer (-255 to 255), y=throttle (-255 to 255)
// Positive y = forward, Negative y = backward
// Positive x = right, Negative x = left
void differentialDrive(int x, int y) {
    x = constrain(x, -255, 255);
    y = constrain(y, -255, 255);

    // Add deadband - ignore small joystick movements
    const int DEADBAND = 20;
    if (abs(x) < DEADBAND) x = 0;
    if (abs(y) < DEADBAND) y = 0;

    // If both are essentially zero, stop motors completely
    if (x == 0 && y == 0) {
        stopMotors();
        return;
    }

    // Differential drive math
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
        case 'F':
            setMotorLeft(-150);  // Forward (both motors)
            setMotorRight(-150);
            break;
        case 'B':
            setMotorLeft(150);   // Backward (both motors)
            setMotorRight(150);
            break;
        case 'L':  // Left pivot - left motor back, right motor forward
        case 'A':
            setMotorLeft(150);   // Left motor BACK
            setMotorRight(-150);  // Right motor FORWARD
            break;
        case 'R':  // Right pivot - left motor forward, right motor back
        case 'D':
            setMotorLeft(-150);  // Left motor FORWARD
            setMotorRight(150);   // Right motor BACK
            break;
        case 'S':
        default:
            stopMotors();
            break;
    }
}

// ============================================================
// Servo Functions
// ============================================================
// Helper: convert angle (0-360) to LEDC duty cycle value
uint32_t angleToDuty(int angle) {
    int pulseUs = map(angle, 0, 360, SERVO_MIN_US, SERVO_MAX_US);
    // duty = pulseUs / periodUs * (2^res - 1)
    uint32_t duty = (uint32_t)((float)pulseUs * ((1 << SERVO_RES) - 1) / (1000000.0 / SERVO_FREQ));
    return duty;
}

void initServos() {
    ledcSetup(PWM_CH_SERVO, SERVO_FREQ, SERVO_RES);
    ledcAttachPin(SPRAY_AIM_PIN, PWM_CH_SERVO);
    ledcWrite(PWM_CH_SERVO, angleToDuty(sprayAimAngle));
    delay(500);
    Serial.println("Spray aim servo ready on GPIO 5 (direct LEDC, 0-360 deg)");
}

void setSprayAim(int angle) {
    sprayAimAngle = constrain(angle, 0, 360);
    ledcWrite(PWM_CH_SERVO, angleToDuty(sprayAimAngle));
    Serial.printf("Spray aim: %d deg (duty=%u)\n", sprayAimAngle, angleToDuty(sprayAimAngle));
}

// ============================================================
// Spray Functions (BO Motor based)
// ============================================================
void initSprayMotors() {
    pinMode(SPRAY_IN1, OUTPUT);
    pinMode(SPRAY_IN3, OUTPUT);

    ledcSetup(PWM_CH_SPRAY_A, 5000, 8);
    ledcSetup(PWM_CH_SPRAY_B, 5000, 8);
    ledcAttachPin(SPRAY_ENA, PWM_CH_SPRAY_A);
    ledcAttachPin(SPRAY_ENB, PWM_CH_SPRAY_B);

    // Ensure motors are off
    digitalWrite(SPRAY_IN1, LOW);
    digitalWrite(SPRAY_IN3, LOW);
    ledcWrite(PWM_CH_SPRAY_A, 0);
    ledcWrite(PWM_CH_SPRAY_B, 0);

    Serial.println("Spray BO motors ready on L298N #2");
}

void triggerSpray(int motorId) {
    unsigned long now = millis();

    if (now - lastSprayTime < SPRAY_COOLDOWN_MS) {
        unsigned long remaining = SPRAY_COOLDOWN_MS - (now - lastSprayTime);
        Serial.printf("Spray on cooldown: %lu ms remaining\n", remaining);
        return;
    }

    if (motorId == 1 && !sprayMotorA.isBusy()) {
        sprayMotorA.start(1);
        lastSprayTime = now;
        Serial.println("Spray motor A (left) triggered");
    } else if (motorId == 2 && !sprayMotorB.isBusy()) {
        sprayMotorB.start(2);
        lastSprayTime = now;
        Serial.println("Spray motor B (right) triggered");
    }
}

unsigned long sprayBothStartTime = 0;
bool sprayBothPending = false;

void triggerSprayBothNonBlocking() {
    unsigned long now = millis();

    if (now - lastSprayTime < SPRAY_COOLDOWN_MS) {
        unsigned long remaining = SPRAY_COOLDOWN_MS - (now - lastSprayTime);
        Serial.printf("Spray on cooldown: %lu ms remaining\n", remaining);
        return;
    }

    if (!sprayMotorA.isBusy() && !sprayMotorB.isBusy()) {
        sprayMotorA.start(1);
        sprayMotorB.start(2);
        lastSprayTime = now;
        Serial.println("Both spray motors triggered");
    }
}

void updateSpraySystem() {
    sprayMotorA.update();
    sprayMotorB.update();
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
            lastWebSocketMessageTime = millis();
            break;

        case WStype_TEXT:
            Serial.printf("[%u] Received: %s\n", num, payload);
            lastWebSocketMessageTime = millis();
            processCommand((char*)payload);
            break;

        case WStype_BIN:
            lastWebSocketMessageTime = millis();
            break;

        default:
            break;
    }
}

// ============================================================
// Command Processor
// ============================================================
void processCommand(char* command) {
    // Check for JSON (starts with '{')
    if (command[0] == '{') {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, command);

        if (!error) {
            const char* type = doc["type"];

            if (strcmp(type, "drive") == 0) {
                int x = doc["x"];
                int y = doc["y"];
                differentialDrive(x, y);
            }
            else if (strcmp(type, "sprayAim") == 0) {
                int angle = doc["angle"];
                setSprayAim(angle);
            }
            else if (strcmp(type, "spray") == 0) {
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
                if (autoSprayEnabled && !pcOffline &&
                    (strcmp(value, "fungus") == 0 || strcmp(value, "pest") == 0)) {
                    triggerSprayBothNonBlocking();
                }
            }
            return;
        }
    }

    // Legacy commands
    if (strncmp(command, "M:", 2) == 0) {
        char dir = command[2];
        driveMotor(dir);
    }
    else if (strncmp(command, "SD:", 3) == 0) {
        int left, right;
        if (sscanf(command + 3, "%d,%d", &left, &right) == 2) {
            setMotorLeft(left);
            setMotorRight(right);
        }
    }
    else if (strncmp(command, "SA:", 3) == 0) {
        int angle = atoi(command + 3);
        setSprayAim(angle);
    }
}

// ============================================================
// HTTP Server Handlers
// ============================================================
void handleRoot() {
    // Serve embedded dashboard HTML with camera feed
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'><title>Plant Rover</title>";
    html += "<style>*{margin:0;padding:0;box-sizing:border-box}body{font-family:sans-serif;background:#0d1117;color:#c9d1d9;min-height:100vh}";
    html += ".top-bar{background:#161b22;padding:12px 20px;display:flex;justify-content:space-between;align-items:center}";
    html += ".logo{font-size:1.3rem;color:#3fb950}.status-dot{width:10px;height:10px;border-radius:50%;background:#f85149;display:inline-block;margin-right:8px}";
    html += ".status-dot.connected{background:#3fb950}.container{max-width:1200px;margin:20px auto;display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:15px}";
    html += ".card{background:#161b22;border-radius:10px;padding:20px;border:1px solid #30363d}";
    html += ".card h2{color:#3fb950;margin-bottom:15px;font-size:1.1rem}";
    html += "#joystick{width:180px;height:180px;border-radius:50%;background:#21262d;margin:20px auto;position:relative;border:2px solid #30363d}";
    html += "#joystickKnob{width:50px;height:50px;border-radius:50%;background:#58a6ff;position:absolute;top:50%;left:50%;transform:translate(-50%,-50%)}";
    html += ".btn{width:100%;padding:15px;border:none;border-radius:8px;font-size:1rem;font-weight:600;cursor:pointer;margin-bottom:10px;transition:all 0.2s}";
    html += ".btn:active{transform:scale(0.98)}.btn-primary{background:#58a6ff;color:white}.btn-danger{background:#f85149;color:white}";
    html += ".btn-warning{background:#d29922;color:white}.btn-success{background:#3fb950;color:white}.dpad{display:grid;grid-template-areas:'. up .' 'left stop right' '. down .';gap:8px;max-width:180px;margin:0 auto}";
    html += ".dpad button{padding:15px;border:none;border-radius:8px;background:#21262d;color:#58a6ff;cursor:pointer;font-size:1.2rem}";
    html += ".dpad button:active{background:#58a6ff;color:white}.dpad .up{grid-area:up}.dpad .down{grid-area:down}.dpad .left{grid-area:left}";
    html += ".dpad .right{grid-area:right}.dpad .stop{grid-area:stop;background:#f85149;color:white}.info{font-size:0.85rem;color:#8b949e;margin-top:10px}";
    html += ".video-container{width:100%;background:#000;border-radius:8px;overflow:hidden;position:relative}";
    html += "#camFeed{width:100%;height:auto;display:block}";
    html += ".video-badge{position:absolute;top:10px;left:10px;background:rgba(0,0,0,0.7);padding:5px 10px;border-radius:5px;font-size:0.75rem;color:#0f0}";
    html += ".video-error{position:absolute;top:0;left:0;width:100%;height:100%;display:flex;align-items:center;justify-content:center;color:#f85149;background:#161b22}</style>";
    html += "</head><body>";
    html += "<div class='top-bar'><div class='logo'>🌱 Plant Rover</div><div><span class='status-dot' id='statusDot'></span><span id='statusText'>Connecting...</span></div></div>";
    html += "<div class='container'>";
    // Camera Feed
    html += "<div class='card'><h2>📹 Camera Feed</h2><div class='video-container'>";
    html += "<img id='camFeed' src='http://192.168.4.2:81/stream' alt='Camera' onerror='this.style.display=\"none\";document.getElementById(\"camError\").style.display=\"flex\"'>";
    html += "<div id='camError' class='video-error' style='display:none'>Camera Offline - Check ESP32-CAM</div>";
    html += "<div class='video-badge'>LIVE</div></div></div>";
    // Joystick card
    html += "<div class='card'><h2>🕹️ Drive Controls</h2><div id='joystick'><div id='joystickKnob'></div></div>";
    html += "<div class='info'>X: <span id='xVal'>0</span> Y: <span id='yVal'>0</span></div></div>";
    // D-Pad
    html += "<div class='card'><h2>🚗 Direction</h2><div class='dpad'>";
    html += "<button class='up' ontouchstart='sendDrive(0,-1)' onmousedown='sendDrive(0,-1)' ontouchend='stopDrive()' onmouseup='stopDrive()'>▲</button>";
    html += "<button class='left' ontouchstart='sendDrive(-1,0)' onmousedown='sendDrive(-1,0)' ontouchend='stopDrive()' onmouseup='stopDrive()'>◀</button>";
    html += "<button class='stop' onclick='stopDrive()'>⬛</button>";
    html += "<button class='right' ontouchstart='sendDrive(1,0)' onmousedown='sendDrive(1,0)' ontouchend='stopDrive()' onmouseup='stopDrive()'>▶</button>";
    html += "<button class='down' ontouchstart='sendDrive(0,1)' onmousedown='sendDrive(0,1)' ontouchend='stopDrive()' onmouseup='stopDrive()'>▼</button>";
    html += "</div></div>";
    // Spray Aim
    html += "<div class='card'><h2>🎯 Spray Aim</h2>";
    html += "<input type='range' class='slider' id='aimSlider' min='0' max='360' value='180' oninput='updateAim(this.value)' style='width:100%'>";
    html += "<div class='info'>Aim: <span id='aimVal'>180</span> deg (0=left, 180=center, 360=right)</div>";
    html += "<button class='btn btn-primary' onclick='aimPreset(0)'>⬅ Left</button>";
    html += "<button class='btn btn-success' onclick='aimPreset(180)'>🏠 Home</button>";
    html += "<button class='btn btn-primary' onclick='aimPreset(360)'>Right ➡</button></div>";
    // Spray Activation
    html += "<div class='card'><h2>💨 Spray Controls</h2>";
    html += "<button class='btn btn-primary' onclick='spray(1)'>🌿 Spray Left Motor</button>";
    html += "<button class='btn btn-primary' onclick='spray(2)'>🌿 Spray Right Motor</button>";
    html += "<button class='btn btn-warning' onclick='spray(\"both\")'>💨 Spray Both</button>";
    html += "<div class='info' style='margin-top:15px'>Auto Spray: <span id='autoStatus'>OFF</span></div>";
    html += "<button class='btn btn-primary' style='margin-top:5px' onclick='toggleAuto()'>Toggle Auto Spray</button></div>";
    // Info
    html += "<div class='card'><h2>📊 Info</h2>";
    html += "<div class='info'>Rover IP: 192.168.4.1</div>";
    html += "<div class='info'>Camera IP: 192.168.4.2</div>";
    html += "<div class='info'>WebSocket: ws://192.168.4.1:81</div>";
    html += "</div>";
    html += "</div>";
    // JavaScript
    html += "<script>let ws,joystickActive=false;const j=document.getElementById('joystick'),k=document.getElementById('joystickKnob');";
    html += "function connect(){ws=new WebSocket('ws://'+window.location.hostname+':81/');";
    html += "ws.onopen=function(){document.getElementById('statusDot').classList.add('connected');document.getElementById('statusText').textContent='Connected';};";
    html += "ws.onclose=function(){document.getElementById('statusDot').classList.remove('connected');document.getElementById('statusText').textContent='Reconnecting...';setTimeout(connect,3000);};";
    html += "ws.onmessage=function(e){console.log(e.data);};}";
    html += "function sendJSON(o){if(ws&&ws.readyState===1)ws.send(JSON.stringify(o));}";
    html += "function sendDrive(x,y){sendJSON({type:'drive',x:x*255,y:y*255});document.getElementById('xVal').textContent=Math.round(x*255);document.getElementById('yVal').textContent=Math.round(y*255);}";
    html += "function stopDrive(){sendJSON({type:'drive',x:0,y:0});document.getElementById('xVal').textContent='0';document.getElementById('yVal').textContent='0';}";
    html += "function updateAim(v){document.getElementById('aimVal').textContent=v;sendJSON({type:'sprayAim',angle:parseInt(v)});}";
    html += "function aimPreset(a){document.getElementById('aimSlider').value=a;updateAim(a);}";
    html += "function spray(id){sendJSON({type:'spray',id:id});}";
    html += "function toggleAuto(){var s=document.getElementById('autoStatus');var on=s.textContent==='OFF';sendJSON({type:'autoSpray',enabled:on});s.textContent=on?'ON':'OFF';}";
    html += "j.addEventListener('mousedown',function(e){joystickActive=true;updateJ(e);});";
    html += "j.addEventListener('touchstart',function(e){joystickActive=true;updateJ(e.touches[0]);e.preventDefault();});";
    html += "document.addEventListener('mousemove',function(e){if(joystickActive)updateJ(e);});";
    html += "document.addEventListener('touchmove',function(e){if(joystickActive){updateJ(e.touches[0]);e.preventDefault();}});";
    html += "document.addEventListener('mouseup',function(){if(joystickActive){joystickActive=false;k.style.transform='translate(-50%,-50%)';stopDrive();}});";
    html += "document.addEventListener('touchend',function(){if(joystickActive){joystickActive=false;k.style.transform='translate(-50%,-50%)';stopDrive();}});";
    html += "function updateJ(e){const r=j.getBoundingClientRect();let dx=e.clientX-r.left-90,dy=e.clientY-r.top-90;";
    html += "const d=Math.sqrt(dx*dx+dy*dy);if(d>90){dx=dx/d*90;dy=dy/d*90;}k.style.transform='translate(calc(-50%+'+dx+'px),calc(-50%+'+dy+'px))';";
    html += "sendJSON({type:'drive',x:Math.round(dx/90*255),y:Math.round(dy/90*255)});document.getElementById('xVal').textContent=Math.round(dx/90*255);document.getElementById('yVal').textContent=Math.round(dy/90*255);}";
    html += "connect();</script></body></html>";

    httpServer.send(200, "text/html", html);
}

void handleStatus() {
    String json = "{";
    json += "\"connected\":true,";
    json += "\"sprayAim\":" + String(sprayAimAngle) + ",";
    json += "\"motorLeft\":" + String(motorSpeedLeft) + ",";
    json += "\"motorRight\":" + String(motorSpeedRight) + ",";
    json += "\"autoSprayEnabled\":" + String(autoSprayEnabled ? "true" : "false") + ",";
    json += "\"sprayABusy\":" + String(sprayMotorA.isBusy() ? "true" : "false") + ",";
    json += "\"sprayBBusy\":" + String(sprayMotorB.isBusy() ? "true" : "false") + ",";
    json += "\"pcOffline\":" + String(pcOffline ? "true" : "false") + ",";
    json += "\"ip\":\"" + WiFi.softAPIP().toString() + "\",";
    json += "\"rover_ip\":\"192.168.4.1\"";
    json += "}";
    httpServer.send(200, "application/json", json);
}

void handleDetection() {
    lastDetectionApiCallTime = millis();

    if (httpServer.method() != HTTP_POST) {
        httpServer.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
        return;
    }

    if (!httpServer.hasArg("plain")) {
        httpServer.send(400, "application/json", "{\"error\":\"No body\"}");
        return;
    }

    String body = httpServer.arg("plain");
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        httpServer.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    const char* label = doc["label"];
    float confidence = doc["confidence"];

    if (label == nullptr || doc["confidence"].isNull()) {
        httpServer.send(400, "application/json", "{\"error\":\"Missing label or confidence\"}");
        return;
    }

    confidence = constrain(confidence, 0.0f, 1.0f);
    Serial.printf("Detection received: label='%s', confidence=%.2f\n", label, confidence);

    if (confidence > 0.70f) {
        lastDetection.set(label, confidence);

        // Broadcast to WebSocket clients
        JsonDocument broadcastDoc;
        broadcastDoc["type"] = "detection";
        broadcastDoc["label"] = label;
        broadcastDoc["confidence"] = confidence;

        String broadcastJson;
        serializeJson(broadcastDoc, broadcastJson);
        webSocket.broadcastTXT(broadcastJson);

        // Trigger auto-spray if enabled and PC is online
        if (autoSprayEnabled && !pcOffline) {
            Serial.println("Auto-spray enabled - triggering spray!");
            triggerSprayBothNonBlocking();
        }
    }

    httpServer.send(200, "application/json", "{\"received\":true}");
}

void handleNotFound() {
    httpServer.send(404, "text/plain", "404: Not found");
}

// ============================================================
// Setup
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
    Serial.println("Servos ready (sprayAim=GPIO5)");

    // Initialize Motors
    Serial.println("Initializing motor driver...");
    initMotors();
    Serial.println("Drive motors ready");

    // Initialize Spray System
    Serial.println("Initializing spray system...");
    initSprayMotors();
    Serial.println("Spray system ready (aim servo + BO motors on L298N #2)");

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

    // Setup WebSocket Server
    Serial.println("Starting WebSocket server...");
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    Serial.printf("WebSocket server running on port %d\n", WS_PORT);

    Serial.println("\n=================================");
    Serial.println("🌱 Plant Rover Ready!");
    Serial.println("=================================\n");
    Serial.println("Open dashboard.html to control the rover");
    Serial.println("\n--- Serial Commands ---");
    Serial.println("Send 'H' or '?' for help");
}

// ============================================================
// Serial Help
// ============================================================
void printSerialHelp() {
    Serial.println("\n=================================");
    Serial.println("      SERIAL COMMANDS");
    Serial.println("=================================\n");
    Serial.println("Drive Commands:");
    Serial.println("  F / W    - Forward");
    Serial.println("  B / S    - Backward");
    Serial.println("  L / A    - Left (pivot)");
    Serial.println("  R / D    - Right (pivot)");
    Serial.println("  X / SP   - Stop Motors");
    Serial.println("\nOther Commands:");
    Serial.println("  H / ?    - Show this help");
    Serial.println("\n=================================\n");
}

// ============================================================
// Main Loop
// ============================================================
void loop() {
    webSocket.loop();
    httpServer.handleClient();
    updateSpraySystem();

    // ============================================================
    // Serial Command Handler
    // ============================================================
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        command.toUpperCase();

        // Ignore empty commands
        if (command.length() == 0) return;

        Serial.print("Serial Command: ");
        Serial.println(command);

        // Single character commands
        if (command.length() == 1) {
            char cmd = command.charAt(0);
            switch (cmd) {
                case 'F':  // Forward
                case 'W':  // WASD style
                    driveMotor('F');
                    Serial.println("-> FORWARD");
                    break;
                case 'B':  // Backward
                case 'S':  // WASD style
                    driveMotor('B');
                    Serial.println("-> BACKWARD");
                    break;
                case 'L':  // Left
                case 'A':  // WASD style
                    driveMotor('L');
                    Serial.println("-> LEFT");
                    break;
                case 'R':  // Right
                case 'D':  // WASD style
                    driveMotor('R');
                    Serial.println("-> RIGHT");
                    break;
                case 'X':  // Stop (also spacebar handled as 'X' for simplicity)
                case ' ':  // Space = stop
                    stopMotors();
                    Serial.println("-> STOP");
                    break;
                case 'H':  // Help
                case '?':  // Help
                    printSerialHelp();
                    break;
                default:
                    Serial.println("Unknown command. Send 'H' or '?' for help.");
                    break;
            }

            // Update watchdog timer so Serial commands don't get interrupted
            lastWebSocketMessageTime = millis();
        }
        // Multi-character commands
        else if (command.startsWith("SPEED ")) {
            int speed = command.substring(6).toInt();
            speed = constrain(speed, 0, 255);
            Serial.printf("-> Set test speed to: %d\n", speed);
            // Update watchdog timer
            lastWebSocketMessageTime = millis();
        }
        else if (command == "HELP") {
            printSerialHelp();
        }
        else {
            Serial.println("Unknown command. Send 'H' or '?' for help.");
        }
    }

    unsigned long now = millis();

    // Watchdog: Stop motors if no WebSocket message for 3 seconds
    if (lastWebSocketMessageTime > 0 && now - lastWebSocketMessageTime > WS_WATCHDOG_TIMEOUT) {
        if (motorSpeedLeft != 0 || motorSpeedRight != 0) {
            Serial.println("Watchdog: No WS message for 3 seconds, stopping motors");
            stopMotors();
        }
    }

    // PC Offline Detection
    if (lastDetectionApiCallTime > 0) {
        bool wasOffline = pcOffline;
        pcOffline = (now - lastDetectionApiCallTime > PC_OFFLINE_TIMEOUT);

        if (pcOffline != wasOffline || pcOffline != previousPcOfflineState) {
            if (pcOffline) {
                Serial.println("PC Offline detected - disabling auto-spray");
                autoSprayEnabled = false;

                JsonDocument eventDoc;
                eventDoc["type"] = "pcOffline";
                eventDoc["offline"] = true;
                String eventJson;
                serializeJson(eventDoc, eventJson);
                webSocket.broadcastTXT(eventJson);
            } else {
                Serial.println("PC Online detected");
                JsonDocument eventDoc;
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
