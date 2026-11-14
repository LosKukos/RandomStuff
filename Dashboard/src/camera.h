#pragma once
#include <Arduino.h>

// Jedna kontrola dostupnosti IP kamery (TCP connect na CAMERA_PORT_A/B), aktualizuje
// stav `cameraState`, pri zmene online/offline posle ntfy zpravu. Blokuje max ~2 s.
void cameraPoll();
