#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// =========================
// Wi-Fi settings
// =========================
const char* ssid = "WIFI";
const char* password = "PASSWORD";

const IPAddress defenderIP(192, 168, 1, 112);
const unsigned int defenderPort = 4210;

WiFiUDP udp;

// =========================
// Button settings
// =========================
const int SEND_BUTTON_PIN = D2;
const int MODE_BUTTON_PIN = D6;

const unsigned long DEBOUNCE_MS = 50;

struct ButtonState {
  int pin;
  bool lastReading;
  bool stableState;
  unsigned long changedAt;
};

ButtonState sendButton = {
  SEND_BUTTON_PIN, HIGH, HIGH, 0
};

ButtonState modeButton = {
  MODE_BUTTON_PIN, HIGH, HIGH, 0
};

// =========================
// Sender modes
// =========================
enum SenderMode {
  MODE_SINGLE,
  MODE_BURST,
  MODE_REPEAT,
  MODE_INVALID
};

SenderMode currentMode = MODE_SINGLE;

// =========================
// Sending settings
// =========================
const unsigned int BURST_COUNT = 5;
const unsigned long BURST_INTERVAL_MS = 250;
const unsigned long REPEAT_INTERVAL_MS = 2000;

bool burstActive = false;
bool repeatActive = false;

unsigned int burstSent = 0;
unsigned long lastSendAt = 0;

// =========================
// Button helpers
// =========================
void initializeButton(ButtonState& button) {
  pinMode(button.pin, INPUT_PULLUP);

  button.lastReading = digitalRead(button.pin);
  button.stableState = button.lastReading;
  button.changedAt = millis();
}

bool buttonPressed(ButtonState& button) {
  bool reading = digitalRead(button.pin);
  unsigned long now = millis();

  if (reading != button.lastReading) {
    button.lastReading = reading;
    button.changedAt = now;
  }

  if (now - button.changedAt >= DEBOUNCE_MS &&
      reading != button.stableState) {
    button.stableState = reading;

    return button.stableState == LOW;
  }

  return false;
}

// =========================
// Mode control
// =========================
void stopSending() {
  burstActive = false;
  repeatActive = false;
}

void printCurrentMode() {
  switch (currentMode) {
    case MODE_SINGLE:
      Serial.println(
        "Mode: SINGLE. Press D2 button to send once."
      );
      break;

    case MODE_BURST:
      Serial.println(
        "Mode: BURST. Press D2 button to send 5 warnings."
      );
      break;

    case MODE_REPEAT:
      Serial.println(
        "Mode: REPEAT. Press D2 button to start or stop."
      );
      break;

    case MODE_INVALID:
      Serial.println(
        "Mode: INVALID. Press D2 button to send FCW_WARN_BROKEN."
      );
      break;
  }
}

void selectMode(SenderMode newMode) {
  bool wasSending = burstActive || repeatActive;

  stopSending();
  currentMode = newMode;

  if (wasSending) {
    Serial.println("Active sending stopped.");
  }

  printCurrentMode();
}

void cycleMode() {
  switch (currentMode) {
    case MODE_SINGLE:
      selectMode(MODE_BURST);
      break;

    case MODE_BURST:
      selectMode(MODE_REPEAT);
      break;

    case MODE_REPEAT:
      selectMode(MODE_INVALID);
      break;

    case MODE_INVALID:
      selectMode(MODE_SINGLE);
      break;
  }
}

// =========================
// Send a text packet
// =========================
bool sendPacket(const char* payload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Not sent: Wi-Fi disconnected.");
    return false;
  }

  if (!udp.beginPacket(defenderIP, defenderPort)) {
    Serial.println("Send failed: could not start UDP packet.");
    return false;
  }

  udp.print(payload);

  if (udp.endPacket() != 1) {
    Serial.println("Send failed: UDP transmission error.");
    return false;
  }

  Serial.print("Sent: ");
  Serial.println(payload);

  return true;
}

bool sendWarning() {
  return sendPacket("FCW_WARN");
}

// =========================
// Serial commands
// =========================
bool handleSerial() {
  bool commandHandled = false;

  while (Serial.available() > 0) {
    char command = Serial.read();

    switch (command) {
      case '1':
        selectMode(MODE_SINGLE);
        commandHandled = true;
        break;

      case '2':
        selectMode(MODE_BURST);
        commandHandled = true;
        break;

      case '3':
        selectMode(MODE_REPEAT);
        commandHandled = true;
        break;

      case '4':
        selectMode(MODE_INVALID);
        commandHandled = true;
        break;

      case '0':
        stopSending();
        Serial.println("Stopped. Waiting for button press.");
        commandHandled = true;
        break;

      case '\r':
      case '\n':
      case ' ':
        break;

      default:
        Serial.println(
          "Commands: 1=single, 2=burst, 3=repeat, 4=invalid, 0=stop."
        );
        break;
    }
  }

  return commandHandled;
}

// =========================
// Send-button action
// =========================
void handleSendButton() {
  if (currentMode == MODE_REPEAT && repeatActive) {
    stopSending();
    Serial.println("Repeat stopped.");
    return;
  }

  if (burstActive) {
    Serial.println("Burst already running; button press ignored.");
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Cannot start: Wi-Fi disconnected.");
    return;
  }

  switch (currentMode) {
    case MODE_SINGLE:
      sendWarning();
      break;

    case MODE_BURST:
      burstSent = 0;
      burstActive = true;
      lastSendAt = millis() - BURST_INTERVAL_MS;
      Serial.println("Burst started.");
      break;

    case MODE_REPEAT:
      repeatActive = true;
      lastSendAt = millis() - REPEAT_INTERVAL_MS;
      Serial.println("Repeat started: one warning every 2 seconds.");
      break;

    case MODE_INVALID:
      sendPacket("FCW_WARN_BROKEN");
      break;
  }
}

// =========================
// Sending scheduler
// =========================
void updateSending() {
  if (!burstActive && !repeatActive) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    stopSending();
    Serial.println("Sending stopped: Wi-Fi disconnected.");
    return;
  }

  unsigned long now = millis();

  unsigned long interval = burstActive
    ? BURST_INTERVAL_MS
    : REPEAT_INTERVAL_MS;

  if (now - lastSendAt < interval) {
    return;
  }

  lastSendAt = now;

  if (!sendWarning()) {
    stopSending();
    Serial.println("Sending stopped after send failure.");
    return;
  }

  if (burstActive) {
    burstSent++;

    Serial.print("Burst progress: ");
    Serial.print(burstSent);
    Serial.print("/");
    Serial.println(BURST_COUNT);

    if (burstSent >= BURST_COUNT) {
      burstActive = false;
      Serial.println("Burst complete. Waiting for button press.");
    }
  }
}

// =========================
// Discard incoming ACKs
// =========================
void drainReplies() {
  int packetSize = udp.parsePacket();

  if (packetSize > 0) {
    char buffer[64];

    while (udp.available() > 0) {
      udp.read(buffer, sizeof(buffer));
    }
  }
}

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(115200);

  initializeButton(sendButton);
  initializeButton(modeButton);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.println();
  Serial.print("Connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Wi-Fi connected");

  Serial.print("Attacker IP: ");
  Serial.println(WiFi.localIP());

  if (udp.begin(4211) != 1) {
    Serial.println("Warning: could not open local UDP port.");
  }

  // Refresh button states after the connection wait.
  initializeButton(sendButton);
  initializeButton(modeButton);

  Serial.println();
  Serial.println("Attacker ready.");
  Serial.println("D2 button = send/start/stop");
  Serial.println("D6 button = change mode");
  Serial.println(
    "Serial: 1=single, 2=burst, 3=repeat, 4=invalid, 0=stop"
  );

  printCurrentMode();
}

// =========================
// Main loop
// =========================
void loop() {
  bool serialControlUsed = handleSerial();

  bool modePressed = buttonPressed(modeButton);
  bool sendPressed = buttonPressed(sendButton);

  // Mode selection takes priority over sending.
  if (modePressed) {
    cycleMode();
  } else if (sendPressed && !serialControlUsed) {
    handleSendButton();
  }

  updateSending();
  drainReplies();
}
