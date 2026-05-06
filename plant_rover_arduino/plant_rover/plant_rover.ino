/**
 * Plant Rover - ESP32 WiFi Controlled Robot
 *
 * Arduino IDE Version
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
 * - WebSocket Server on port 81
 * - Differential steering
 * - Spray system with rate limiting
 * - Watchdog and PC offline detection
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
const int MOTOR_A_IN1 = 25;
const int MOTOR_A_IN2 = 26;
const int MOTOR_A_ENA = 32;  // PWM

const int MOTOR_B_IN3 = 27;
const int MOTOR_B_IN4 = 14;
const int MOTOR_B_ENB = 33;  // PWM

const int PWM_FREQUENCY = 5000;
const int PWM_RESOLUTION = 8;  // 8-bit = 0-255
const int PWM_CHANNEL_A = 0;
const int PWM_CHANNEL_B = 1;

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

    void update() {
        if (!active) return;

        unsigned long now = millis();

        switch (state) {
            case SPRAY_DEPLOY:
                if (servoNum == 2 || servoNum == 3) {
                    if (servoNum == 2) servo2.write(120);
                    else servo3.write(120);
                }
                state = SPRAY_HOLD;
                stateStartTime = now;
                break;

            case SPRAY_HOLD:
                if (now - stateStartTime >= 800) {
                    state = SPRAY_RETRACT;
                }
                break;

            case SPRAY_RETRACT:
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

    ledcSetup(PWM_CHANNEL_A, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcSetup(PWM_CHANNEL_B, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcAttachPin(MOTOR_A_ENA, PWM_CHANNEL_A);
    ledcAttachPin(MOTOR_B_ENB, PWM_CHANNEL_B);

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

// Differential steering: x=steer (-255 to 255), y=throttle (-255 to 255)
void differentialDrive(int x, int y) {
    x = constrain(x, -255, 255);
    y = constrain(y, -255, 255);

    int leftSpeed = y - x;
    int rightSpeed = y + x;

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
            setMotorLeft(150);
            setMotorRight(150);
            break;
        case 'B':
            setMotorLeft(-150);
            setMotorRight(-150);
            break;
        case 'L':
            setMotorLeft(-150);
            setMotorRight(150);
            break;
        case 'R':
            setMotorLeft(150);
            setMotorRight(-150);
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
void initServos() {
    servo1.attach(SERVO1_PIN);
    servo2.attach(SERVO2_PIN);
    servo3.attach(SERVO3_PIN);

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
        }
    } else if (id == 3) {
        if (!sprayController3.isBusy()) {
            sprayController3.start(3);
            lastSprayTime = now;
            Serial.println("Spray 3 triggered");
        }
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

    if (!sprayController2.isBusy() && !sprayController3.isBusy()) {
        sprayController2.start(2);
        sprayBothStartTime = now;
        sprayBothPending = true;
        lastSprayTime = now;
        Serial.println("Both sprays triggered");
    }
}

void updateSpraySystem() {
    sprayController2.update();
    sprayController3.update();

    if (sprayBothPending && (millis() - sprayBothStartTime >= 200)) {
        sprayController3.start(3);
        sprayBothPending = false;
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
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, command);

        if (!error) {
            const char* type = doc["type"];

            if (strcmp(type, "drive") == 0) {
                int x = doc["x"];
                int y = doc["y"];
                differentialDrive(x, y);
            }
            else if (strcmp(type, "servo1") == 0) {
                int angle = doc["angle"];
                setServoAngle(1, angle);
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
    // Servo Home
    html += "<div class='card'><h2>🔧 Servo Home</h2>";
    html += "<button class='btn btn-success' onclick='goHome()'>🏠 Home (90°)</button>";
    html += "<div class='info'>Sets motors & spray to face forward</div>";
    html += "<input type='range' class='slider' id='servoSlider' min='0' max='180' value='90' oninput='updateServo(this.value)' style='margin-top:15px'>";
    html += "<div class='info'>Angle: <span id='servoVal'>90</span>°</div></div>";
    // Spray
    html += "<div class='card'><h2>💨 Spray Controls</h2>";
    html += "<button class='btn btn-primary' onclick='spray(2)'>🌿 Spray Left</button>";
    html += "<button class='btn btn-primary' onclick='spray(3)'>🌿 Spray Right</button>";
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
    html += "function updateServo(v){document.getElementById('servoVal').textContent=v;sendJSON({type:'servo1',angle:parseInt(v)});}";
    html += "function goHome(){document.getElementById('servoSlider').value=90;updateServo(90);}";
    html += "function spray(id){sendJSON({type:'spray',id:id});}";
    html += "function toggleAuto(){sendJSON({type:'autoSpray',enabled:true});document.getElementById('autoStatus').textContent='ON';}";
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
    json += "\"servo1\":" + String(servo1Angle) + ",";
    json += "\"servo2\":" + String(servo2Angle) + ",";
    json += "\"servo3\":" + String(servo3Angle) + ",";
    json += "\"motorLeft\":" + String(motorSpeedLeft) + ",";
    json += "\"motorRight\":" + String(motorSpeedRight) + ",";
    json += "\"autoSprayEnabled\":" + String(autoSprayEnabled ? "true" : "false") + ",";
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
    StaticJsonDocument<256> doc;
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
        StaticJsonDocument<256> broadcastDoc;
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

    // Setup WebSocket Server
    Serial.println("Starting WebSocket server...");
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    Serial.printf("WebSocket server running on port %d\n", WS_PORT);

    Serial.println("\n=================================");
    Serial.println("🌱 Plant Rover Ready!");
    Serial.println("=================================\n");
    Serial.println("Open dashboard.html to control the rover");
}

// ============================================================
// Main Loop
// ============================================================
void loop() {
    webSocket.loop();
    httpServer.handleClient();
    updateSpraySystem();

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

                StaticJsonDocument<128> eventDoc;
                eventDoc["type"] = "pcOffline";
                eventDoc["offline"] = true;
                String eventJson;
                serializeJson(eventDoc, eventJson);
                webSocket.broadcastTXT(eventJson);
            } else {
                Serial.println("PC Online detected");
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
