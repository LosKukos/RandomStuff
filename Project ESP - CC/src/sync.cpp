#include "sync.h"

static SemaphoreHandle_t dataMutex = nullptr;  // rekurzivni, aby se handlery mohly volat navzajem
static SemaphoreHandle_t logMutex  = nullptr;

void initSync() {
  if (!dataMutex) dataMutex = xSemaphoreCreateRecursiveMutex();
  if (!logMutex)  logMutex  = xSemaphoreCreateMutex();
}

DataLock::DataLock() : held_(false) {
  if (dataMutex) { xSemaphoreTakeRecursive(dataMutex, portMAX_DELAY); held_ = true; }
}
DataLock::~DataLock() {
  if (held_) xSemaphoreGiveRecursive(dataMutex);
}

LogLock::LogLock() : held_(false) {
  if (logMutex) { xSemaphoreTake(logMutex, portMAX_DELAY); held_ = true; }
}
LogLock::~LogLock() {
  if (held_) xSemaphoreGive(logMutex);
}
