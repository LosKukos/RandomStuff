# Project ESP - CC · AXIS

Logistics control system for Minecraft (ComputerCraft + Create + AE2) bridged through an ESP32.

---

## Architecture

```
MC (ComputerCraft)
  ├── master       — AE2 export + Create packager pipeline
  ├── node         — package checkpoint scanner
  └── loader_gate  — train loader gate

        │  HTTP / WebSocket
        ▼

ESP32 (AXIS controller)
  ├── REST API     — orders, packages, nodes, commands
  ├── WebSocket    — real-time events to local dashboard
  ├── LittleFS     — persistent storage
  └── AP mode      — WiFi setup interface

        │  browser
        ▼

Local dashboard (index.html served by ESP)
```

---

## ESP32 Firmware

Built with PlatformIO + Arduino framework.

### Dependencies

```ini
lib_deps =
  bblanchon/ArduinoJson
  esp32async/ESPAsyncWebServer
  esp32async/AsyncTCP
```

### First setup

1. Flash firmware via PlatformIO.
2. Upload `data/` folder via LittleFS upload tool.
3. Connect to WiFi AP `AP client` (password `1234567890`).
4. Open `http://192.168.4.1` and set your WiFi credentials.
5. ESP will connect to your network and be reachable at its local IP.

### Source structure

```
src/
  main.cpp          — boot, task init
  app_state.h/cpp   — shared state, structs
  web.h/cpp         — REST API + WebSocket routes
  wifi_task.h/cpp   — WiFi STA + AP management
  command_task.h/cpp — command queue + LittleFS flush
  commands.h/cpp    — command push + WS emit
  orders.h/cpp      — order CRUD
  packages.h/cpp    — package CRUD + history
  nodes.h/cpp       — node registration + heartbeat
  persistence.h/cpp — LittleFS load/save
  time_service.h/cpp — NTP, Europe/Prague timezone
  utils.h/cpp       — helpers
```

---

## ComputerCraft

### Install

Download `install.lua` to a CC computer with HTTP enabled and run:

```lua
install
```

Installs either `master` or `node` package from GitHub into `axis/master/` or `axis/node/`.

### Packages

**master** — runs on the factory CC computer. Polls ESP for pending orders, exports items from AE2, feeds Create packager, registers packages back to ESP.

**node** — dumb checkpoint scanner. Detects Create packages passing through a depot and reports them to ESP.

**loader_gate** — train loader gate. Claims next packed order from ESP, accepts/rejects packages on a sorting belt, confirms load to ESP, then releases the train.

### CC source structure

```
CC/
  installer/   — install.lua
  master/      — config.lua, master.lua, esp.lua, me.lua, factory.lua, orders.lua, util.lua
  node/        — config.lua, node.lua, esp.lua, scanner.lua, util.lua
  loader/      — loader_gate.lua
```

---

## ESP API

All endpoints are STA-only (local network) except `/api/config` and `/api/debug` which are AP-only.

### Orders

| Method | Path | Description |
|---|---|---|
| POST | `/api/orders/create` | Create new order |
| POST | `/api/orders/pending` | List pending orders |
| POST | `/api/orders/get` | Get single order |
| POST | `/api/orders/update` | Update order status |
| POST | `/api/orders/claim-next-load` | Claim next packed order for loading |
| POST | `/api/orders/load-complete` | Confirm all packages loaded |

### Packages

| Method | Path | Description |
|---|---|---|
| POST | `/api/package/register` | Register new package |
| POST | `/api/package/event` | Report node scan event |
| POST | `/api/package/loaded` | Confirm package physically loaded to train |
| POST | `/api/package/get` | Get single package |
| POST | `/api/packages/by-order` | List packages for order |

### Nodes

| Method | Path | Description |
|---|---|---|
| POST | `/api/node/register` | Register or re-register node |
| POST | `/api/node/heartbeat` | Node heartbeat |
| GET  | `/api/nodes/list` | List all nodes |

### Commands

| Method | Path | Description |
|---|---|---|
| POST | `/api/command` | Queue command (e.g. craft) |
| POST | `/api/ack` | Acknowledge command |
| POST | `/api/result` | Post command result |

### System

| Method | Path | Description |
|---|---|---|
| GET | `/api/status` | System status (STA) |
| GET | `/api/debug` | Debug info (AP) |
| GET | `/api/logs` | Log buffer (AP) |
| POST | `/api/config` | Save WiFi config (AP) |
| POST | `/api/me/list` | Push ME storage snapshot |
| GET  | `/api/me/list` | Get ME storage snapshot |

---

## Local Dashboard

Served by ESP at `http://<esp-ip>/` on STA interface.

Features:
- ME storage browser with search
- Craft command trigger
- Order basket — build and submit orders
- Order tracking with route visualization
- Real-time updates via WebSocket

---

## Notes

- LittleFS persists orders, packages, nodes, commands and ME cache across reboots.
- Runtime files (`config.json`, `me.json`, `orders.json`, etc.) are excluded from Git.
- NTP syncs to `pool.ntp.org` on WiFi connect, timezone Europe/Prague.
- CC node IDs are assigned by ESP on first registration and stored locally in `node_state.json` (not committed).
