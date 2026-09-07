/*
  MAYAP I2C/LCD DIAGNOSTIC - sketch RIENG, DOC LAP hoan toan voi firmware
  chinh (MAYAP_INDUSTRIAL_v3_4_0) - khong include machine_control.h/hmi.h/
  realtime_link.h/cloud_alert_link.h, khong dieu khien relay/cam bien/dao
  trung, khong chay nhieu task FreeRTOS tranh chap I2C nhu firmware chinh.

  MUC DICH: co May Ap dang thinh thoang bi vo/soc man hinh (nghi do nhieu
  I2C giua ESP32 va LCD). Sketch nay CHI lam 1 viec: ve lien tuc 1 hoa tiet
  co dinh (de mat thay ngay neu bi loi 1-2 diem) + dem SO LAN GIAO DICH I2C
  THAT BAI (doc lap voi u8g2, kiem tra thang qua Wire) + ghi Serial de doi
  chieu voi luc nguoi dung thay man hinh vo bang mat.

  Vi sao thiet ke DON GIAN het muc (1 vong lap, khong FreeRTOS/mutex/task
  nhu firmware chinh): neu man hinh VAN vo/soc voi sketch toi gian nay - noi
  khong con code dieu khien phuc tap co the gay tranh chap I2C - thi kha
  nang cao la do PHAN CUNG (day I2C, dien tro keo len, nguon, nhieu tu dong
  co/relay/bo doi nguon...). Neu sketch nay chay ON DINH khong loi, nhung
  firmware chinh van bi, thi loi nhieu kha nang nam trong CODE/LICH CHAY cua
  firmware chinh (tranh chap mutex, task khac chiem CPU dung luc dang ve...).

  ------------------------------- CHUAN BI TRUOC KHI NAP -------------------
  1) Sua WIFI_SSID / WIFI_PASSWORD ben duoi thanh Wi-Fi that cua ban (bat
     buoc de dung duoc OTA nap lai qua mang - KHONG the OTA ngay tu dau, lan
     nap DAU TIEN van phai qua cap USB nhu binh thuong).
  2) Neu can, doi OTA_PASSWORD (mac dinh giong firmware chinh: "181020").
  3) Kiem tra dong "U8G2_ST7567_..." ben duoi co dung loai panel LCD may ban
     dang dung khong (xem ghi chu ngay canh - firmware chinh co 2 loai, chon
     qua LCD_PROFILE trong config.h).

  ------------------------------- NAP LAI QUA WI-FI (OTA) ------------------
  Sau khi nap lan dau qua USB va ESP32 da ket noi duoc Wi-Fi (xem Serial
  Monitor 115200 baud de biet da noi chua): mo Arduino IDE > Tools > Port,
  se thay 1 muc dang "mayap-i2c-test at xxx.xxx.xxx.xxx (Network Port)" -
  chon muc do roi bam Upload nhu binh thuong, KHONG can rut cap USB.
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <ArduinoOTA.h>

// ---------------------------- Wi-Fi (BAT BUOC SUA) -------------------------
static const char *WIFI_SSID = "TEN_WIFI_CUA_BAN";
static const char *WIFI_PASSWORD = "MAT_KHAU_WIFI_CUA_BAN";
static const char *OTA_PASSWORD = "181020";
static const char *OTA_HOSTNAME = "mayap-i2c-test";

// ---------------------------- Phan cung (giong firmware chinh) -------------
// Xem MAYAP_INDUSTRIAL_v3_4_0/config.h: LCD_I2C_ADDRESS/PIN_I2C_SDA/
// PIN_I2C_SCL/I2C_CLOCK_HZ - giu nguyen gia tri that de test dung dieu kien
// thuc te (khac gia tri o day se khong con phan anh dung phan cung that).
constexpr uint8_t PIN_I2C_SDA = 8;
constexpr uint8_t PIN_I2C_SCL = 9;
constexpr uint8_t LCD_I2C_ADDRESS = 0x3F;
constexpr uint32_t I2C_CLOCK_HZ = 100000UL;

// Doi thanh U8G2_ST7567_JLX12864_F_HW_I2C(...) neu may ban dung loai panel
// do (LCD_PROFILE == 2 trong firmware chinh) - chi duoc BAT 1 trong 2 dong.
U8G2_ST7567_ENH_DG128064I_F_HW_I2C lcd(U8G2_R0, U8X8_PIN_NONE);
// U8G2_ST7567_JLX12864_F_HW_I2C lcd(U8G2_R0, U8X8_PIN_NONE);

static uint32_t frameCount = 0;
static uint32_t i2cErrorCount = 0;
static uint32_t recoverCount = 0;
static uint32_t lastHealthCheckAt = 0;
static uint32_t lastLogAt = 0;
static uint32_t lastWifiRetryAt = 0;

// Bus-recovery giong het firmware chinh (xem hmi.h, ham i2cBusRecoverLocked
// tuong duong) - "danh thuc" I2C khi SDA bi ket muc thap qua lau (thuong do
// LCD dang giua 1 giao dich thi mat xung/nhieu). Neu sketch toi gian nay
// cung phai goi ham nay thuong xuyen, do la 1 tin hieu manh cho thay van de
// nam o duong day/nguon dien, khong phai o cach firmware chinh viet code.
void i2cBusRecover() {
  Wire.end();
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_I2C_SCL, HIGH);
  if (digitalRead(PIN_I2C_SDA) == LOW) {
    for (uint8_t i = 0; i < 9 && digitalRead(PIN_I2C_SDA) == LOW; ++i) {
      digitalWrite(PIN_I2C_SCL, LOW);
      delayMicroseconds(5);
      digitalWrite(PIN_I2C_SCL, HIGH);
      delayMicroseconds(5);
    }
  }
  pinMode(PIN_I2C_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_I2C_SDA, LOW);
  delayMicroseconds(5);
  digitalWrite(PIN_I2C_SCL, HIGH);
  delayMicroseconds(5);
  digitalWrite(PIN_I2C_SDA, HIGH);
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);
  Wire.setTimeOut(25);
  ++recoverCount;
}

// Kiem tra bus con "song" khong bang 1 giao dich I2C THANG (khong qua
// u8g2) - cho ket qua khach quan, doc lap voi viec mat thuong co nhin ra
// man hinh loi hay khong (nhieu loi I2C chi mat 1 vai byte, khong luon lam
// vo anh ro den muc nguoi dung de y).
bool i2cHealthCheck() {
  Wire.beginTransmission(LCD_I2C_ADDRESS);
  const uint8_t result = Wire.endTransmission();
  if (result != 0) {
    ++i2cErrorCount;
    Serial.printf(
        "[I2C] LOI ma=%u tai frame=%lu t=%lums (tong so loi=%lu, tong lan "
        "phai recover bus=%lu)\n",
        result, static_cast<unsigned long>(frameCount), millis(),
        static_cast<unsigned long>(i2cErrorCount),
        static_cast<unsigned long>(recoverCount));
    i2cBusRecover();
    lcd.begin();
    return false;
  }
  return true;
}

void setupOta() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { Serial.println("[OTA] Bat dau nap firmware moi..."); });
  ArduinoOTA.onEnd([]() { Serial.println("[OTA] Nap xong, chuan bi khoi dong lai."); });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] THAT BAI ma loi=%d\n", static_cast<int>(error));
  });
  ArduinoOTA.begin();
  Serial.printf("[OTA] San sang nhan firmware qua Wi-Fi (%s.local)\n", OTA_HOSTNAME);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== MAYAP I2C/LCD DIAGNOSTIC ===");
  Serial.println("Sketch rieng, khong lien quan firmware dieu khien chinh.");

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);
  Wire.setTimeOut(25);
  lcd.setBusClock(I2C_CLOCK_HZ);
  lcd.setI2CAddress(static_cast<uint8_t>(LCD_I2C_ADDRESS << 1));
  lcd.begin();
  lcd.clearBuffer();
  lcd.setFont(u8g2_font_6x12_tf);
  lcd.drawStr(2, 20, "I2C/LCD TEST");
  lcd.drawStr(2, 36, "Dang noi Wi-Fi...");
  lcd.sendBuffer();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Dang ket noi \"%s\"...\n", WIFI_SSID);
  const uint32_t wifiStartAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartAt < 15000UL) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WIFI] Da ket noi, IP=%s RSSI=%ddBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    setupOta();
  } else {
    Serial.println("[WIFI] KHONG ket noi duoc trong 15s - kiem tra lai "
                    "WIFI_SSID/WIFI_PASSWORD o dau file, sketch van chay "
                    "phan test LCD binh thuong (chi khong OTA duoc).");
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  } else if (millis() - lastWifiRetryAt >= 10000UL) {
    // Wi-Fi ban dau khong vao duoc hoac bi rot giua chung - thu lai dinh ky,
    // khong chan vong lap ve man hinh (khong dung delay() cho ket noi lai).
    lastWifiRetryAt = millis();
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  const uint32_t now = millis();

  if (now - lastHealthCheckAt >= 500UL) {
    lastHealthCheckAt = now;
    i2cHealthCheck();
  }

  lcd.clearBuffer();
  lcd.setFont(u8g2_font_6x12_tf);
  char line[32];
  snprintf(line, sizeof(line), "FRAME %lu", static_cast<unsigned long>(frameCount));
  lcd.drawStr(2, 12, line);
  snprintf(line, sizeof(line), "LOI I2C: %lu (RC %lu)",
           static_cast<unsigned long>(i2cErrorCount),
           static_cast<unsigned long>(recoverCount));
  lcd.drawStr(2, 26, line);
  snprintf(line, sizeof(line), "WIFI: %s %ddBm",
           WiFi.status() == WL_CONNECTED ? "OK" : "MAT", WiFi.RSSI());
  lcd.drawStr(2, 40, line);
  snprintf(line, sizeof(line), "UPTIME: %lus",
           static_cast<unsigned long>(now / 1000UL));
  lcd.drawStr(2, 54, line);

  // Hoa tiet o co doi mau moi frame - phu kin phan con lai man hinh (64->
  // het chieu cao). Deu tam tap nen CHI CAN 1 pixel sai la thay ngay bang
  // mat, hieu qua hon nhieu so voi chu/so (de lan loi nho vao noi dung).
  const uint8_t phase = static_cast<uint8_t>(frameCount % 2U);
  for (int16_t y = 0; y < 64; y += 4) {
    for (int16_t x = 0; x < 128; x += 4) {
      const bool on = (((x / 4) + (y / 4) + phase) % 2) == 0;
      if (on) lcd.drawBox(x, y, 4, 4);
    }
  }

  lcd.sendBuffer();
  ++frameCount;

  if (now - lastLogAt >= 5000UL) {
    lastLogAt = now;
    Serial.printf(
        "[STATUS] frame=%lu loi_i2c=%lu recover_bus=%lu heap=%u wifi=%s "
        "rssi=%ddBm uptime=%lus\n",
        static_cast<unsigned long>(frameCount),
        static_cast<unsigned long>(i2cErrorCount),
        static_cast<unsigned long>(recoverCount),
        static_cast<unsigned>(ESP.getFreeHeap()),
        WiFi.status() == WL_CONNECTED ? "OK" : "MAT", WiFi.RSSI(),
        static_cast<unsigned long>(now / 1000UL));
  }

  delay(120);  // ~8 khung/giay - gan voi nhip ve thuc te cua HMI firmware chinh
}
