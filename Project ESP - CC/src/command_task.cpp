#include "command_task.h"
#include "app_state.h"
#include "utils.h"
#include "persistence.h"
#include <algorithm>

void commandTask(void* pvParameters) {
  (void)pvParameters;

  uint32_t lastQueueSave = 0;
  uint32_t lastMeSave = 0;

  for (;;) {
    uint32_t now = millis();

    for (auto& cmd : commandQueue) {
      if (cmd.status == "queued") {
        cmd.status = "sent";
        cmd.updated = now;
        queueDirty = true;
      }

      if (cmd.status == "sent" && (now - cmd.updated > 10000)) {
        cmd.status = "timeout";
        cmd.updated = now;
        queueDirty = true;
        addLog("[CMD] timeout " + cmd.id);
      }
    }

    commandQueue.erase(
      std::remove_if(commandQueue.begin(), commandQueue.end(),
        [now](const Command& cmd) {
          bool finished = (
            cmd.status == "done" ||
            cmd.status == "failed" ||
            cmd.status == "partial" ||
            cmd.status == "timeout"
          );
          return finished && (now - cmd.updated > 60000);
        }),
      commandQueue.end()
    );

    if (queueDirty && (now - lastQueueSave > 2000)) {
      saveQueue();
      lastQueueSave = now;
    }

    if (meDirty && (now - lastMeSave > 5000)) {
      saveME();
      lastMeSave = now;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
