#pragma once
// Zámky pro sdílená data. Pravidla (viz patch/README.md):
//  1) DataLock = vezmi ho, kdykoli sahas na orders/packages/nodes/users/commandQueue/meStorage/playersOnline.
//  2) LogLock  = jen uvnitr addLog() a cteni logs. Je "listovy" - uvnitr nej nikdy nebereme jiny zamek.
//  3) Poradi: DataLock -> (outbox v mqtt_bridge) . Nikdy obracene.
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

void initSync();   // zavolat jako prvni v setup()

class DataLock {
 public:
  DataLock();
  ~DataLock();
  DataLock(const DataLock&) = delete;
  DataLock& operator=(const DataLock&) = delete;
 private:
  bool held_;
};

class LogLock {
 public:
  LogLock();
  ~LogLock();
  LogLock(const LogLock&) = delete;
  LogLock& operator=(const LogLock&) = delete;
 private:
  bool held_;
};
