#pragma once
#include <Arduino.h>

// inicializace DHT11 (vnitrni) + DHT22 (venkovni) - volat jednou
void initSensors();

// precte oba senzory a ulozi do state.h `climate` pod climateMutex.
// Vraci true, pokud se aspon jeden senzor uspesne precetl.
bool pollClimate();
