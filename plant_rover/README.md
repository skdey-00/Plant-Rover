# Plant Rover - ESP32 WiFi Controlled Robot

An ESP32-based robot with WiFi control, servos, and L298N motor driver.

## Hardware

| Component | GPIO Pin | Description |
|-----------|----------|-------------|
| Servo 1 | 18 | First servo motor |
| Servo 2 | 19 | Second servo motor |
| Servo 3 | 21 | Third servo motor |
| L298N IN1 | 25 | Motor A direction 1 |
| L298N IN2 | 26 | Motor A direction 2 |
| L298N IN3 | 27 | Motor B direction 1 |
| L298N IN4 | 14 | Motor B direction 2 |
| L298N ENA | 32 | Motor A PWM (Left) |
| L298N ENB | 33 | Motor B PWM (Right) |

## L298N Motor Driver Wiring

```
ESP32              L298N
------------------------
GPIO 25    ---->   IN1
GPIO 26    ---->   IN2
GPIO 27    ---->   IN3
GPIO 14    ---->   IN4
GPIO 32    ---->   ENA
GPIO 33    ---->   ENB
```

Motor A = Left Motor
Motor B = Right Motor

## Features

- **WiFi Access Point**: Connect to "PlantRover" (password: "rover1234")
- **HTTP Dashboard**: Web interface at http://192.168.4.1
- **WebSocket Control**: Real-time bidirectional communication on port 81
- **3x Servo Control**: 0-180 degree range
- **Dual Motor Control**: Forward, backward, left, right with adjustable PWM speed

## WebSocket Commands

| Command | Format | Description |
|---------|--------|-------------|
| Motor Direction | `M:F` | Forward |
| Motor Direction | `M:B` | Backward |
| Motor Direction | `M:L` | Left pivot |
| Motor Direction | `M:R` | Right pivot |
| Motor Direction | `M:S` | Stop |
| Motor Speed | `SD:left,right` | Set speeds 0-255 |
| Servo 1 | `S1:angle` | Set servo 1 (0-180) |
| Servo 2 | `S2:angle` | Set servo 2 (0-180) |
| Servo 3 | `S3:angle` | Set servo 3 (0-180) |

## Building & Uploading

```bash
# Build the project
pio run

# Upload to ESP32
pio run --target upload

# Monitor serial output
pio device monitor
```

Or use the PlatformIO extension in VS Code.

## Power Requirements

- ESP32: 5V via USB or 3.3V regulator
- Servos: 5V (external supply recommended for 3 servos)
- L298N: Motor power supply (6-12V recommended for DC motors)
- Common ground between all power supplies is essential!

## License

MIT
