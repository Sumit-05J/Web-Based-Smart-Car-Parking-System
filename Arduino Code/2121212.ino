#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

const char* ssid = "Tarun's A15";
const char* password = "44444444";

const int IR_PINS[4] = {33, 32, 25, 26};

bool occupied[4] = {0,0,0,0};
bool prevOccupied[4] = {0,0,0,0};
bool reserved[4] = {0,0,0,0};

const unsigned long RESERVE_DURATION_MS = 60000UL;
unsigned long reserveStart[4] = {0,0,0,0};

const int LED_PINS[4] = {2, 4, 12, 13};

unsigned long entryTime[4] = {0,0,0,0};
unsigned long totalStayMinutes = 0;
int totalCarsCount = 0;

Servo gateServo;

const int SERVO_PIN = 5;
const int GATE_OPEN_ANGLE = 90;
const int GATE_CLOSE_ANGLE = 0;

const int ENTRY_SENSOR_PIN = 27;
const bool ENTRY_ACTIVE_LEVEL = LOW;

enum GateState {
  GATE_CLOSED,
  GATE_OPEN,
  GATE_WAIT_CLEAR
};

GateState gateState = GATE_CLOSED;
unsigned long gateTimer = 0;

LiquidCrystal_I2C lcd(0x27, 16, 2);

bool lcdAwake = true;
unsigned long lastActivityTime = 0;
const unsigned long IDLE_TIMEOUT_MS = 30000UL;

WebServer server(80);

void recordActivity() {
  lastActivityTime = millis();

  if (!lcdAwake) {
    lcd.backlight();
    lcdAwake = true;
  }
}

char slotChar(int i) {
  if (occupied[i]) return 'O';
  if (reserved[i]) return 'R';
  return 'E';
}

void updateLEDs() {
  for (int i = 0; i < 4; i++) {
    bool on = occupied[i] || ((i < 2) && reserved[i]);
    digitalWrite(LED_PINS[i], on ? HIGH : LOW);
  }
}

void updateLCD() {
  int occ = 0, res = 0;

  for (int i = 0; i < 4; i++) {
    if (occupied[i]) occ++;
    else if (reserved[i]) res++;
  }

  lcd.setCursor(0, 0);
  lcd.print("F:");
  lcd.print(4 - occ - res);
  lcd.print(" O:");
  lcd.print(occ);
  lcd.print(" R:");
  lcd.print(res);
  lcd.print("   ");

  lcd.setCursor(0, 1);

  for (int i = 0; i < 4; i++) {
    lcd.print(i + 1);
    lcd.print(":");
    lcd.print(slotChar(i));

    if (i < 3) lcd.print(" ");
  }

  lcd.print(" ");
}

void updateGate() {
  bool sensorActive =
      (digitalRead(ENTRY_SENSOR_PIN) == ENTRY_ACTIVE_LEVEL);

  unsigned long now = millis();

  switch (gateState) {

    case GATE_CLOSED:
      if (sensorActive) {
        gateServo.write(GATE_OPEN_ANGLE);
        gateTimer = now;
        gateState = GATE_OPEN;
        recordActivity();
      }
      break;

    case GATE_OPEN:
      if (now - gateTimer >= 5000UL) {
        gateServo.write(GATE_CLOSE_ANGLE);
        gateState = GATE_WAIT_CLEAR;
      }
      break;

    case GATE_WAIT_CLEAR:
      if (!sensorActive) {
        gateState = GATE_CLOSED;
      }
      break;
  }
}

void handleStatus() {
  server.sendHeader("Access-Control-Allow-Origin", "*");

  StaticJsonDocument<256> doc;

  JsonArray slots = doc.createNestedArray("slots");

  for (int i = 0; i < 4; i++) {
    JsonObject s = slots.createNestedObject();
    s["id"] = i + 1;
    s["occupied"] = occupied[i];
    s["reserved"] = reserved[i];
  }

  String out;
  serializeJson(doc, out);

  server.send(200, "application/json", out);
}

void handleSummary() {
  server.sendHeader("Access-Control-Allow-Origin", "*");

  StaticJsonDocument<128> doc;

  doc["totalCars"] = totalCarsCount;
  doc["avgStayMinutes"] =
      totalCarsCount ? (float)totalStayMinutes / totalCarsCount : 0;

  String out;
  serializeJson(doc, out);

  server.send(200, "application/json", out);
}

void handleToggle() {
  server.sendHeader("Access-Control-Allow-Origin", "*");

  if (!server.hasArg("slot")) {
    server.send(400, "Missing slot");
    return;
  }

  int idx = server.arg("slot").toInt() - 1;

  if (idx < 0 || idx > 1 || occupied[idx]) {
    server.send(403, "Not allowed");
    return;
  }

  reserved[idx] = !reserved[idx];
  reserveStart[idx] = reserved[idx] ? millis() : 0;

  recordActivity();

  server.send(200, "OK");
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    pinMode(IR_PINS[i], INPUT);
    pinMode(LED_PINS[i], OUTPUT);
  }

  pinMode(ENTRY_SENSOR_PIN, INPUT);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(" SMART PARKING ");

  lcd.setCursor(0, 1);
  lcd.print("    SYSTEM    ");

  delay(2000);
  lcd.clear();

  gateServo.attach(SERVO_PIN);
  gateServo.write(GATE_CLOSE_ANGLE);

  WiFi.begin(ssid, password);

  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  server.on("/status", handleStatus);
  server.on("/summary", handleSummary);
  server.on("/toggle", handleToggle);

  server.begin();

  Serial.println("Web Server Started");

  recordActivity();
}

void loop() {
  server.handleClient();

  for (int i = 0; i < 4; i++) {
    occupied[i] = (digitalRead(IR_PINS[i]) == LOW);
  }

  updateGate();
  updateLCD();
  updateLEDs();

  if (lcdAwake &&
      millis() - lastActivityTime >= IDLE_TIMEOUT_MS) {
    lcd.noBacklight();
    lcdAwake = false;
  }

  delay(100);
}