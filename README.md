# Smart Fridge Monitoring System

Sistem monitoring kulkas berbasis ESP32. Alat ini memantau suhu, kelembapan, dan status pintu kulkas, lalu memberi peringatan lewat LED, buzzer, LCD, dan MQTT kalau suhu terlalu tinggi atau pintu terbuka terlalu lama.

Proyek ini dibuat dengan PlatformIO dan bisa disimulasikan di Wokwi.

## Fitur

- Baca suhu dan kelembapan pakai DHT22 (setiap 2 detik)
- Deteksi pintu terbuka dan hitung berapa lama terbukanya
- Alarm suhu kalau suhu > **5.0 °C**
- Alarm pintu kalau pintu terbuka ≥ **10 detik**
- Indikator LED:
  - Hijau: normal
  - Kuning: pintu terbuka
  - Merah (berkedip): ada alarm
- Buzzer berbunyi putus-putus saat alarm, bisa dibisukan lewat tombol atau perintah MQTT
- LCD 16x2 I2C menampilkan suhu, kelembapan, dan status pintu
- Kirim data ke broker MQTT setiap 5 detik, dan langsung dikirim saat status berubah

## Komponen

| Komponen | Pin ESP32 |
|---|---|
| DHT22 | GPIO 13 |
| Sensor pintu (slide switch, HIGH = terbuka) | GPIO 32 |
| Tombol mute | GPIO 33 |
| Buzzer | GPIO 25 |
| LED hijau | GPIO 14 |
| LED kuning | GPIO 27 |
| LED merah | GPIO 26 |
| LCD 1602 I2C (alamat 0x27) | SDA 21, SCL 22 |

Rangkaian lengkapnya ada di [diagram.json](diagram.json).

## Struktur Folder

```
├── src/main.cpp      # Program utama
├── platformio.ini    # Konfigurasi board & library
├── diagram.json      # Rangkaian Wokwi
└── wokwi.toml        # Konfigurasi simulator Wokwi
```

## Cara Menjalankan

### Simulasi di Wokwi (VS Code)

1. Install extension **PlatformIO IDE** dan **Wokwi Simulator** di VS Code.
2. Build project: `pio run` (atau klik tombol Build di PlatformIO).
3. Buka `diagram.json`, lalu jalankan **Wokwi: Start Simulator**.
4. Suhu DHT22 bisa diubah dengan klik sensornya, dan pintu dibuka/tutup lewat slide switch.

### Upload ke ESP32 asli

1. Ganti `WIFI_SSID` dan `WIFI_PASS` di [src/main.cpp](src/main.cpp) sesuai WiFi yang dipakai.
2. Upload: `pio run -t upload`
3. Buka serial monitor: `pio device monitor` (baud 115200)

## MQTT

Broker: `broker.hivemq.com` port `1883`

| Topik | Arah | Isi |
|---|---|---|
| `kulkasguard/kel-es/kulkas1/data` | ESP32 → dashboard | Data sensor (JSON) |
| `kulkasguard/kel-es/kulkas1/status` | ESP32 → dashboard | Status sistem (retained) |
| `kulkasguard/kel-es/kulkas1/cmd` | dashboard → ESP32 | Perintah, misalnya `MUTE` |

Contoh payload data:

```json
{
  "suhu": 3.0,
  "kelembapan": 55,
  "pintu": "tertutup",
  "durasi_buka": 0,
  "status": "NORMAL",
  "dibisukan": false,
  "batas_suhu": 5.0
}
```

Kalau sensor gagal dibaca, `suhu` dan `kelembapan` bernilai `null`.

Nilai status yang mungkin: `NORMAL`, `PINTU_TERBUKA`, `SENSOR_ERROR`, `ALARM_SUHU`, `ALARM_PINTU`, `ALARM_SUHU_PINTU`.

Untuk tes cepat bisa pakai [HiveMQ WebSocket Client](https://www.hivemq.com/demos/websocket-client/): subscribe ke `kulkasguard/kel-es/kulkas1/#`, lalu publish `MUTE` ke topik `cmd` untuk membisukan alarm.

## Library

- [DHT sensor library for ESPx](https://github.com/beegee-tokyo/DHTesp)
- [LiquidCrystal_I2C](https://github.com/marcoschwartz/LiquidCrystal_I2C)
- [PubSubClient](https://github.com/knolleary/pubsubclient)

Semua library otomatis diunduh oleh PlatformIO dari `platformio.ini`.
