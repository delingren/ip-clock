#include <WiFiManager.h>
#include <TM1637Display.h>
#include <sntp.h>
#include <time.h>

/**
* Captive portal settings
*/
const int provisionButton = 0;
const uint32_t provisionPressMs = 3000;
const char* provisionSSID = "IP-Clock";
WiFiManager wifiManager;

/**
* TM1637 display settings
*/
TM1637Display display(/* clk = */ 4, /* dio = */ 5);

// Bit 0x40 on the second digit (index 1) controls the center colon on clock modules
const uint8_t COLON_MASK = 0x40;

/**
 * NTP server and timezone setup
 */
const char* ntpServer = "north-america.pool.ntp.org";
const char* timeZone = "PST8PDT,M3.2.0,M11.1.0";

/**
 * Timer setup
 */
hw_timer_t* timer = NULL;
volatile SemaphoreHandle_t timerSemaphore;
void ARDUINO_ISR_ATTR onTimer() { xSemaphoreGiveFromISR(timerSemaphore, NULL); }

void setup() {
  Serial.begin(115200);
  pinMode(provisionButton, INPUT_PULLUP);
  WiFi.mode(WIFI_STA);

  Serial.println("Setting up WiFi...");

  // One minute timeout
  wifiManager.setConfigPortalTimeout(60);

  // Use saved WiFi credentials; open the portal if it fails
  if (!wifiManager.autoConnect(provisionSSID)) {
    Serial.println("WiFi setup failed. Rebooting in 3s...");
    delay(3000);
    ESP.restart();
  }

  Serial.println("WiFi setup complete.");

  // Sync time with NTP server
  configTzTime(timeZone, ntpServer);

  // Wait for the sync to be completed.
  Serial.println("Waiting for NTP time synchronization...");
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.print("\nTime synchronized: ");
  Serial.println(time(nullptr));

  // Set up timer to trigger an update every second
  timerSemaphore = xSemaphoreCreateBinary();
  // Set timer resolution to 1Mhz; trigger the timer ervery 1M cycles (1s).
  timer = timerBegin(/* frequency = */ 1000000);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, /* alarm_value = */
             500000, /* autoreload = */ true,
             /* reload_count = */ 0);
}

void loop() {
  static uint32_t pressedAt = 0;
  if (digitalRead(provisionButton) == LOW) {
    if (!pressedAt) { 
      pressedAt = millis();
    }
    if (millis() - pressedAt > provisionPressMs) {
      Serial.println("Entering provisioning mode...");
      wifiManager.startConfigPortal(provisionSSID);
      // Reboot regardless if provisioning was successful
      Serial.println("Rebooting in 3s...");
      delay(3000);
      ESP.restart();
    }
  } else {
    pressedAt = 0;
  }

  // Update once per second.
  if (xSemaphoreTake(timerSemaphore, 0) != pdTRUE) {
    return;
  }

  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  updateTimeDisplay(timeinfo);

  static int day = 0;
  if (timeinfo->tm_mday != day) {
    if (day != 0) {
      // Resync with NTP server once a day at midnight
      configTzTime(timeZone, ntpServer);
    }
    day = timeinfo->tm_mday;
  }

  sntp_sync_status_t syncStatus = sntp_get_sync_status();
  if (syncStatus == SNTP_SYNC_STATUS_COMPLETED) {
    Serial.println("Time synchronized.");
  }
}

void updateTimeDisplay(tm *timeinfo) {
  static bool colonOn = false;
  uint8_t digits[4];
  int hour = timeinfo->tm_hour;
  int minute = timeinfo->tm_min;

  digits[0] = display.encodeDigit(hour / 10);
  digits[1] = display.encodeDigit(hour % 10);
  digits[2] = display.encodeDigit(minute / 10);
  digits[3] = display.encodeDigit(minute % 10);

  if (colonOn) {
    digits[1] |= COLON_MASK;
  }
  colonOn = !colonOn;

  Serial.print(int(hour/10));
  Serial.print(int(hour%10));
  Serial.print(colonOn ? ":" : " ");
  Serial.print(int(minute/10));
  Serial.print(int(minute%10));
  Serial.print(colonOn ? ":" : " ");
  Serial.print(int(timeinfo->tm_sec/10));
  Serial.print(int(timeinfo->tm_sec%10));
  Serial.println();
  
  display.setSegments(digits);
}