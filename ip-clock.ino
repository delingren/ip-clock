#include <WiFiManager.h>
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
             1000000, /* autoreload = */ true,
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
  Serial.printf("%02d:%02d:%02d\n", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
}