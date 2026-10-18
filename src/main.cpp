#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHTesp.h>
#include <LiquidCrystal_I2C.h>

#define PIN_DHT         13
#define PIN_PINTU       32   // HIGH = pintu terbuka
#define PIN_MUTE        33
#define PIN_BUZZER      25
#define PIN_LED_HIJAU   14
#define PIN_LED_KUNING  27
#define PIN_LED_MERAH   26

const float BATAS_SUHU = 5.0;
const unsigned long BATAS_PINTU_MS = 10000;

const char* WIFI_SSID   = "Wokwi-GUEST";
const char* WIFI_PASS   = "";
const char* MQTT_SERVER = "broker.hivemq.com";
const int   MQTT_PORT   = 1883;

const char* TOPIK_DATA   = "kulkasguard/kel-es/kulkas1/data";
const char* TOPIK_STATUS = "kulkasguard/kel-es/kulkas1/status";
const char* TOPIK_CMD    = "kulkasguard/kel-es/kulkas1/cmd";

DHTesp dht;
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient espClient;
PubSubClient mqtt(espClient);

float suhu = 0;
float kelembapan = 0;
bool sensorOk = false;

bool pintuTerbuka = false;
unsigned long waktuPintuBuka = 0;
unsigned long durasiBuka = 0;

bool alarmSuhu = false;
bool alarmPintu = false;
bool dibisukan = false;
bool buzzerNyala = false;
bool tombolSebelumnya = HIGH;
bool kirimSegera = false;
const char* statusTerakhir = "";

unsigned long tBaca = 0, tKirim = 0, tLcd = 0, tMqtt = 0, tTombol = 0;

const char* statusSistem() {
  if (alarmSuhu && alarmPintu) return "ALARM_SUHU_PINTU";
  if (alarmSuhu)               return "ALARM_SUHU";
  if (alarmPintu)              return "ALARM_PINTU";
  if (!sensorOk)               return "SENSOR_ERROR";
  if (pintuTerbuka)            return "PINTU_TERBUKA";
  return "NORMAL";
}

void bacaSensor() {
  TempAndHumidity data = dht.getTempAndHumidity();
  sensorOk = (dht.getStatus() == 0);
  if (sensorOk) {
    suhu = data.temperature;
    kelembapan = data.humidity;
  }
}

void bacaPintu(unsigned long now) {
  bool terbuka = (digitalRead(PIN_PINTU) == HIGH);
  if (terbuka && !pintuTerbuka) waktuPintuBuka = now;
  pintuTerbuka = terbuka;
  durasiBuka = pintuTerbuka ? now - waktuPintuBuka : 0;
}

void bisukanAlarm(const char* sumber) {
  if ((alarmSuhu || alarmPintu) && !dibisukan) {
    dibisukan = true;
    kirimSegera = true;
    Serial.printf("[INFO] Alarm dibisukan lewat %s\n", sumber);
  }
}

void cekTombolMute(unsigned long now) {
  bool tombol = digitalRead(PIN_MUTE);
  if (tombol == LOW && tombolSebelumnya == HIGH && now - tTombol > 200) {
    tTombol = now;
    bisukanAlarm("tombol");
  }
  tombolSebelumnya = tombol;
}

void aturOutput(unsigned long now) {
  bool adaAlarm = alarmSuhu || alarmPintu;

  digitalWrite(PIN_LED_HIJAU,  (!adaAlarm && !pintuTerbuka && sensorOk) ? HIGH : LOW);
  digitalWrite(PIN_LED_KUNING, pintuTerbuka ? HIGH : LOW);
  digitalWrite(PIN_LED_MERAH,  (adaAlarm && (now / 300) % 2 == 0) ? HIGH : LOW);

  bool harusBunyi = adaAlarm && !dibisukan && (now / 500) % 2 == 0;
  if (harusBunyi && !buzzerNyala) {
    tone(PIN_BUZZER, 2000);
    buzzerNyala = true;
  } else if (!harusBunyi && buzzerNyala) {
    noTone(PIN_BUZZER);
    buzzerNyala = false;
  }
}

void tulisBaris(uint8_t baris, const char* teks) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", teks);
  lcd.setCursor(0, baris);
  lcd.print(buf);
}

void updateLcd() {
  char b1[17], b2[17];

  if (sensorOk) snprintf(b1, sizeof(b1), "T:%5.1fC H:%3.0f%%", suhu, kelembapan);
  else          snprintf(b1, sizeof(b1), "Sensor error!");

  unsigned long dtk = durasiBuka / 1000;
  if (alarmSuhu && alarmPintu) snprintf(b2, sizeof(b2), "!SUHU & PINTU!");
  else if (alarmSuhu)          snprintf(b2, sizeof(b2), "!SUHU TINGGI!");
  else if (alarmPintu)         snprintf(b2, sizeof(b2), "!PINTU BUKA %lus", dtk);
  else if (pintuTerbuka)       snprintf(b2, sizeof(b2), "Pintu buka %lus", dtk);
  else                         snprintf(b2, sizeof(b2), "Pintu tertutup");

  tulisBaris(0, b1);
  tulisBaris(1, b2);
}

void kirimData() {
  if (!mqtt.connected()) return;

  char suhuStr[10], rhStr[10];
  if (sensorOk) {
    snprintf(suhuStr, sizeof(suhuStr), "%.1f", suhu);
    snprintf(rhStr, sizeof(rhStr), "%.0f", kelembapan);
  } else {
    strcpy(suhuStr, "null");
    strcpy(rhStr, "null");
  }

  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"suhu\":%s,\"kelembapan\":%s,\"pintu\":\"%s\",\"durasi_buka\":%lu,"
           "\"status\":\"%s\",\"dibisukan\":%s,\"batas_suhu\":%.1f}",
           suhuStr, rhStr, pintuTerbuka ? "terbuka" : "tertutup", durasiBuka / 1000,
           statusSistem(), dibisukan ? "true" : "false", BATAS_SUHU);

  mqtt.publish(TOPIK_DATA, payload);
}

void kirimStatus(const char* st) {
  Serial.printf("[STATUS] %s\n", st);
  if (!mqtt.connected()) return;
  mqtt.publish(TOPIK_STATUS, st, true);
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String pesan;
  for (unsigned int i = 0; i < length; i++) pesan += (char)payload[i];
  pesan.trim();
  pesan.toUpperCase();
  Serial.printf("[MQTT] %s -> %s\n", topic, pesan.c_str());

  if (pesan == "MUTE") bisukanAlarm("dashboard");
}

void jagaMqtt(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  if (tMqtt != 0 && now - tMqtt < 5000) return;
  tMqtt = now;

  String clientId = "smartfridge-" + String((uint32_t)esp_random(), HEX);
  Serial.print("[MQTT] Menghubungkan... ");
  if (mqtt.connect(clientId.c_str())) {
    Serial.println("terhubung");
    mqtt.subscribe(TOPIK_CMD);
    kirimStatus(statusSistem());
    kirimData();
  } else {
    Serial.printf("gagal (rc=%d), coba lagi 5 detik\n", mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_PINTU, INPUT_PULLUP);
  pinMode(PIN_MUTE, INPUT_PULLUP);
  pinMode(PIN_LED_HIJAU, OUTPUT);
  pinMode(PIN_LED_KUNING, OUTPUT);
  pinMode(PIN_LED_MERAH, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  dht.setup(PIN_DHT, DHTesp::DHT22);

  lcd.init();
  lcd.backlight();
  tulisBaris(0, "  Smart Fridge");
  tulisBaris(1, "Connect WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
  Serial.print("[WiFi] Menghubungkan");
  unsigned long mulai = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - mulai < 10000) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf(" OK, IP %s\n", WiFi.localIP().toString().c_str());
    tulisBaris(1, "WiFi OK");
  } else {
    Serial.println(" gagal, jalan offline dulu");
    tulisBaris(1, "WiFi gagal");
  }
  delay(800);

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(512);

  bacaSensor();
}

void loop() {
  unsigned long now = millis();

  jagaMqtt(now);
  mqtt.loop();

  if (now - tBaca >= 2000) {   // DHT22 minimal 2 detik
    tBaca = now;
    bacaSensor();
    Serial.printf("Suhu %.1f C | RH %.0f%% | Pintu %s (%lus) | %s\n",
                  suhu, kelembapan, pintuTerbuka ? "terbuka" : "tertutup",
                  durasiBuka / 1000, statusSistem());
  }

  bacaPintu(now);
  alarmSuhu  = sensorOk && suhu > BATAS_SUHU;
  alarmPintu = pintuTerbuka && durasiBuka >= BATAS_PINTU_MS;
  if (!alarmSuhu && !alarmPintu) dibisukan = false;

  cekTombolMute(now);
  aturOutput(now);

  const char* st = statusSistem();
  if (strcmp(st, statusTerakhir) != 0) {
    statusTerakhir = st;
    kirimStatus(st);
    kirimSegera = true;
  }

  if (kirimSegera || now - tKirim >= 5000) {
    kirimSegera = false;
    tKirim = now;
    kirimData();
  }

  if (now - tLcd >= 500) {
    tLcd = now;
    updateLcd();
  }

  delay(10);
}
