#pragma once

#include <Arduino.h>
#include "app_state.h"

void emitCommandWS(const Command& cmd);
void pushCommand(const String& type, const String& payloadJson);
