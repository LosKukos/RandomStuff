local ESP_IP = "10.0.1.17"
local ESP_BASE = "http://" .. ESP_IP
local WS_URL = "ws://" .. ESP_IP .. "/ws"
local BRIDGE_SIDE = "right"   -- uprav podle reality
local SYNC_INTERVAL = 10      -- sekundy
local LIMIT = 500             -- max itemů do snapshotu

local ae = peripheral.wrap(BRIDGE_SIDE)
if not ae then
  error("AE bridge not found on " .. BRIDGE_SIDE)
end

local lastJson = nil

-- =========================
-- HTTP helper
-- =========================
local function postJson(path, tbl)
  local res, err = http.post(
    ESP_BASE .. path,
    textutils.serialiseJSON(tbl),
    { ["Content-Type"] = "application/json" }
  )

  if not res then
    print("POST failed:", path, err)
    return false, err
  end

  local body = res.readAll()
  res.close()
  return true, body
end

-- =========================
-- Build storage snapshot
-- =========================
local function buildSnapshot()
  local items = ae.getItems()
  local out = { items = {} }

  local n = 0
  for _, item in ipairs(items) do
    if n >= LIMIT then break end

    local count = item.count or 0
    local craftable = item.isCraftable or false

    if count > 0 or craftable then
      table.insert(out.items, {
        name = item.name,
        displayName = item.displayName or item.name,
        count = count,
        isCraftable = craftable,
        maxStackSize = item.maxStackSize or 64
      })
      n = n + 1
    end
  end

  return out
end

-- =========================
-- Storage sync loop
-- =========================
local function syncLoop()
  while true do
    local ok, snapshot = pcall(buildSnapshot)

    if not ok then
      print("Snapshot build failed:", snapshot)
    else
      local json = textutils.serialiseJSON(snapshot)

      if json ~= lastJson then
        print("Storage changed, syncing...")
        local pushed, body = postJson("/api/me/list", snapshot)

        if pushed then
          lastJson = json
          print("Sync OK:", body)
        end
      else
        print("No storage changes")
      end
    end

    sleep(SYNC_INTERVAL)
  end
end

-- =========================
-- ACK / RESULT helpers
-- =========================
local function ackCommand(id)
  return postJson("/api/ack", {
    id = id
  })
end

local function finishCommand(id, status, reason)
  local payload = {
    id = id,
    status = status
  }

  if reason then
    payload.reason = reason
  end

  return postJson("/api/result", payload)
end

-- =========================
-- Craft execution
-- =========================
local function performCraft(itemName, count)
  -- Jednoduchá varianta.
  -- Pokud bude třeba, později doplníme crafting CPU kontrolu.
  return ae.craftItem({
    name = itemName,
    count = count
  })
end

local function handleCraftCommand(cmd)
  local payload = cmd.payload or {}
  local item = payload.item
  local count = tonumber(payload.count) or 1

  if not item then
    finishCommand(cmd.id, "failed", "missing_item")
    return
  end

  -- Ověření craftability přímo proti snapshotu bridge
  local items = ae.getItems()
  local craftable = false

  for _, it in ipairs(items) do
    if it.name == item then
      craftable = it.isCraftable or false
      break
    end
  end

  if not craftable then
    finishCommand(cmd.id, "failed", "not_craftable")
    return
  end

  local okAck = ackCommand(cmd.id)
  if not okAck then
    print("ACK failed for", cmd.id)
  end

  local ok, result = pcall(function()
    return performCraft(item, count)
  end)

  if ok and result then
    finishCommand(cmd.id, "done")
  elseif ok and not result then
    finishCommand(cmd.id, "failed", "craft_returned_false")
  else
    finishCommand(cmd.id, "failed", tostring(result))
  end
end

-- =========================
-- WS listener loop
-- =========================
local function commandLoop()
  while true do
    print("Connecting WS...")
    local ws, err = http.websocket(WS_URL)

    if not ws then
      print("WS connect failed:", err)
      sleep(3)
    else
      print("WS connected")

      while true do
        local msg = ws.receive()
        if not msg then
          print("WS disconnected")
          break
        end

        local ok, data = pcall(textutils.unserialiseJSON, msg)

        if not ok or not data then
          print("Invalid WS JSON:", msg)
        else
          if data.event == "command" and data.type == "craft" then
            handleCraftCommand(data)
          end
        end
      end

      pcall(function() ws.close() end)
      sleep(2)
    end
  end
end

-- =========================
-- Run both loops
-- =========================
parallel.waitForAny(syncLoop, commandLoop)
