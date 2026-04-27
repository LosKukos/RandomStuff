#include <Arduino.h>
#include <LittleFS.h>
#include "app_state.h"
#include "utils.h"
#include "persistence.h"
#include "wifi_task.h"
#include "web.h"
#include "command_task.h"

void setup() {
  Serial.begin(115200);
  delay(300);

  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS mount failed");
  }

  addLog("[BOOT] starting");

  loadConfig();
  loadQueue();
  loadME();

  xTaskCreatePinnedToCore(
    wifiTask,
    "wifiTask",
    4096,
    nullptr,
    1,
    &wifiTaskHandle,
    0
  );

  xTaskCreatePinnedToCore(
    webTask,
    "webTask",
    8192,
    nullptr,
    1,
    &webTaskHandle,
    1
  );

  xTaskCreatePinnedToCore(
    commandTask,
    "commandTask",
    6144,
    nullptr,
    1,
    &commandTaskHandle,
    1
  );
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
