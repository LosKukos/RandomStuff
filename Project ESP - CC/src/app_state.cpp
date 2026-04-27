#include "app_state.h"

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

TaskHandle_t wifiTaskHandle = nullptr;
TaskHandle_t webTaskHandle = nullptr;
TaskHandle_t commandTaskHandle = nullptr;

volatile bool staConnected = false;
String staSsid = "";
String staPass = "";

std::vector<String> logs;
std::vector<Command> commandQueue;

String meStorage = R"({"items":[]})";
uint32_t meLastUpdate = 0;

bool queueDirty = false;
bool meDirty = false;
