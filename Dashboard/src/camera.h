#pragma once
#include <Arduino.h>

// Jedna kontrola dostupnosti IP kamery (TCP connect na CAMERA_PORT_A/B), aktualizuje
// stav `cameraState`, pri zmene online/offline posle ntfy zpravu. Blokuje max ~2 s.
void cameraPoll();

// Historie zmen stavu (od nejnovejsi): epoch = unixovy cas (0 = cas jeste nebyl znamy),
// online = stav, do ktereho kamera presla. Vraci pocet zapsanych polozek (max `max`).
int cameraHistory(uint32_t *epoch, bool *online, int max);
