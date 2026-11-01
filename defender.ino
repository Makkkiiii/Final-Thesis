#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <string.h>

// =========================
// Wi-Fi settings
// =========================
const char* WIFI_SSID = "WIFI";
const char* WIFI_PASSWORD = "PASSWORD";

WiFiUDP udp;
const unsigned int UDP_PORT = 4210;
char incomingPacket[255];

// =========================
// Pin setup
// =========================
const int TRIG_PIN   = 5;
const int ECHO_PIN   = 18;

const int GREEN_LED  = 2;   // Rejected warning
const int RED_LED    = 4;   // Accepted warning
const int BUZZER_PIN = 17;  // Transistor -> buzzer

// =========================
// IMU setup
// =========================
const byte IMU_ADDR = 0x68;

// Desk-scale demo threshold only
const float CLOSE_DISTANCE_CM = 100.0;

// =========================
// Non-blocking output timers
// =========================
bool warningOutputActive = false;
bool warningBuzzerActive = false;

unsigned long warningOutputStartedAt = 0;

const unsigned long BUZZER_DURATION_MS = 500;
const unsigned long LED_DURATION_MS = 1000;

// =========================
// Utility functions
// =========================
int16_t read16(byte highByte, byte lowByte) {
  return (int16_t)((highByte << 8) | lowByte);
}

void writeIMU(byte reg, byte value) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

bool readIMU(int16_t &ax, int16_t &ay, int16_t &az,
             int16_t &gx, int16_t &gy, int16_t &gz) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(0x3B);

  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom((int)IMU_ADDR, 14) != 14) {
    return false;
  }

  byte data[14];

  for (int i = 0; i < 14; i++) {
    data[i] = Wire.read();
  }

  ax = read16(data[0], data[1]);
  ay = read16(data[2], data[3]);
  az = read16(data[4], data[5]);

  gx = read16(data[8], data[9]);
  gy = read16(data[10], data[11]);
  gz = read16(data[12], data[13]);

  return true;
}

float readDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000UL);

  if (duration == 0) {
    return -1.0;
  }

  return (duration * 0.0343) / 2.0;
}

void buzzerOn() {
  digitalWrite(BUZZER_PIN, HIGH);
}

void buzzerOff() {
  digitalWrite(BUZZER_PIN, LOW);
}

void outputsOff() {
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);
  buzzerOff();
}

// =========================
// Output control
// =========================
void warningAccepted(float distance) {
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, HIGH);
  buzzerOn();

  warningOutputStartedAt = millis();
  warningOutputActive = true;
  warningBuzzerActive = true;

  Serial.println("RESULT: ACCEPTED - nearby obstacle supports warning.");
  Serial.print("Distance used: ");
  Serial.print(distance, 1);
  Serial.println(" cm");
}

void warningRejected(float distance) {
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);
  buzzerOff();

  warningOutputStartedAt = millis();
  warningOutputActive = true;
  warningBuzzerActive = false;

  Serial.println("RESULT: REJECTED - no nearby obstacle supports warning.");

  if (distance < 0) {
    Serial.println("Distance used: No echo / unavailable");
  } else {
    Serial.print("Distance used: ");
    Serial.print(distance, 1);
    Serial.println(" cm");
  }
}

void updateWarningOutputs() {
  if (!warningOutputActive) {
    return;
  }

  unsigned long elapsed = millis() - warningOutputStartedAt;

  if (warningBuzzerActive && elapsed >= BUZZER_DURATION_MS) {
    buzzerOff();
    warningBuzzerActive = false;
  }

  if (elapsed >= LED_DURATION_MS) {
    outputsOff();

    warningOutputActive = false;
    warningBuzzerActive = false;
  }
}

// =========================
// Wi-Fi connection
// =========================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Wi-Fi connected");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());

  udp.begin(UDP_PORT);

  Serial.print("UDP listening on port ");
  Serial.println(UDP_PORT);
}

// =========================
// Warning processing
// =========================
void processWarning() {
  float distance = readDistanceCM();

  int16_t ax, ay, az, gx, gy, gz;
  bool imuOK = readIMU(ax, ay, az, gx, gy, gz);

  Serial.println("----- Local evidence -----");

  if (distance < 0) {
    Serial.println("Distance: No echo");
  } else {
    Serial.print("Distance: ");
    Serial.print(distance, 1);
    Serial.println(" cm");
  }

  if (imuOK) {
    Serial.print("ACC raw: ");
    Serial.print(ax);
    Serial.print(", ");
    Serial.print(ay);
    Serial.print(", ");
    Serial.println(az);

    Serial.print("GYRO raw: ");
    Serial.print(gx);
    Serial.print(", ");
    Serial.print(gy);
    Serial.print(", ");
    Serial.println(gz);
  } else {
    Serial.println("IMU read failed - using distance-only decision.");
  }

  // Existing distance-only rule is unchanged.
  // IMU readings are logged but do not affect this decision.
  if (distance > 0 && distance <= CLOSE_DISTANCE_CM) {
    warningAccepted(distance);
  } else {
    warningRejected(distance);
  }
}

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  outputsOff();

  Wire.begin(21, 22);

  writeIMU(0x6B, 0x00);
  delay(100);

  Wire.beginTransmission(IMU_ADDR);
  Wire.write(0x75);
  Wire.endTransmission(false);
  Wire.requestFrom((int)IMU_ADDR, 1);

  if (Wire.available()) {
    Serial.print("IMU WHO_AM_I: 0x");
    Serial.println(Wire.read(), HEX);
  } else {
    Serial.println("Could not read IMU WHO_AM_I.");
  }

  connectWiFi();

  Serial.println();
  Serial.println("Defender ready.");
  Serial.println("Waiting for FCW_WARN messages...");
}

// =========================
// Main loop
// =========================
void loop() {
  updateWarningOutputs();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi lost - reconnecting...");
    WiFi.reconnect();
    delay(1000);
    return;
  }

  int packetSize = udp.parsePacket();

  if (packetSize > 0) {
    int length = udp.read(
      incomingPacket,
      sizeof(incomingPacket) - 1
    );

    // Do not process an unsuccessful or empty read.
    if (length <= 0) {
      return;
    }

    incomingPacket[length] = '\0';

    Serial.println();
    Serial.print("Received from ");
    Serial.print(udp.remoteIP());
    Serial.print(": ");
    Serial.println(incomingPacket);

    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.print("ACK_FROM_ESP32");
    udp.endPacket();

    if (strcmp(incomingPacket, "FCW_WARN") == 0) {
      processWarning();
    } else {
      Serial.println("Ignored: unrecognized message.");
    }
  }
}
