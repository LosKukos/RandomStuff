local ESP_IP = "10.0.1.17"
local BRIDGE_NAME = "me_bridge_0"
local SYNC_INTERVAL = 5
local LIMIT = 10000

local ESP_BASE = "http://" .. ESP_IP
local WS_URL = "ws://" .. ESP_IP .. "/ws"

local ae = peripheral.wrap(BRIDGE_NAME)
if not ae then
  error("AE bridge not found: " .. BRIDGE_NAME)
end

local lastJson = nil

-- =========================
-- Helpers
-- =========================
local function log(...)
  local parts = {}
  for i = 1, select("#", ...) do
    parts[#parts + 1] = tostring(select(i, ...))
  end
  print(table.concat(parts, " "))
end

local function postJson(path, tbl)
  local body = textutils.serialiseJSON(tbl)

  local res, err = http.post(
    ESP_BASE .. path,
    body,
    { ["Content-Type"] = "application/json" }
  )

  if not res then
    log("POST failed:", path, err)
    return false, err
  end

  local txt = res.readAll()
  res.close()
  return true, txt
end

local function isEmptyTable(t)
  return t == nil or next(t) == nil
end

local function normaliseItem(item)
  return {
    name = item.name,
    displayName = item.displayName or item.name,
    count = item.count or 0,
    hasPattern = false,
    maxStackSize = item.maxStackSize or 64
  }
end

local function normaliseMissingItem(item)
  return {
    name = item.name or "unknown",
    displayName = item.displayName or item.name or "Unknown",
    count = item.count or 0
  }
end

-- =========================
-- Build merged snapshot
-- =========================
local function buildSnapshot()
  local stored = ae.getItems()
  local craftable = ae.getCraftableItems()

  local map = {}

  -- stored items
  for _, item in ipairs(stored) do
    local key = item.name
    map[key] = normaliseItem(item)
  end

  -- craftable items
  for _, item in ipairs(craftable) do
    local key = item.name

    if map[key] then
      map[key].hasPattern = true
    else
      map[key] = {
        name = item.name,
        displayName = item.displayName or item.name,
        count = 0,
        hasPattern = true,
        maxStackSize = item.maxStackSize or 64
      }
    end
  end

  local out = { items = {} }

  for _, item in pairs(map) do
    table.insert(out.items, item)
  end

  table.sort(out.items, function(a, b)
    return string.lower(a.displayName or a.name) < string.lower(b.displayName or b.name)
  end)

  -- hard limit, ať nesežereme ESP/browser
  if #out.items > LIMIT then
    local trimmed = { items = {} }
    for i = 1, LIMIT do
      trimmed.items[i] = out.items[i]
    end
    return trimmed
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
      log("Snapshot build failed:", snapshot)
    else
      local json = textutils.serialiseJSON(snapshot)

      if json ~= lastJson then
        log("Storage changed, syncing...")
        local pushed, body = postJson("/api/me/list", snapshot)

        if pushed then
          lastJson = json
          log("Sync OK:", body)
        end
      else
        log("No storage changes")
      end
    end

    sleep(SYNC_INTERVAL)
  end
end

-- =========================
-- Command result helpers
-- =========================
local function ackCommand(id)
  return postJson("/api/ack", {
    id = id
  })
end

local function finishCommand(payload)
  return postJson("/api/result", payload)
end

-- =========================
-- Craft inspection
-- =========================
local function inspectCraftTask(task, requestedCount)
  -- krátké čekání, aby planner stihl dopočítat stav
  sleep(0.5)

  local hasErr = task.hasErrorOccurred and task.hasErrorOccurred() or false
  local calcFail = task.isCalculationNotSuccessful and task.isCalculationNotSuccessful() or false
  local debugMsg = task.getDebugMessage and task.getDebugMessage() or nil
  local output = task.getFinalOutput and task.getFinalOutput() or nil
  local missing = task.getMissingItems and task.getMissingItems() or nil

  if hasErr or calcFail then
    return {
      status = "failed",
      requested = requestedCount,
      accepted = 0,
      reason = debugMsg or "calculation_failed",
      missing = {}
    }
  end

  local missingList = {}
  if type(missing) == "table" then
    for _, item in ipairs(missing) do
      table.insert(missingList, normaliseMissingItem(item))
    end
  end

  local outputCount = 0
  if type(output) == "table" then
    outputCount = output.count or 0
  end

  -- full success
  if output ~= nil and isEmptyTable(missing) then
    return {
      status = "done",
      requested = requestedCount,
      accepted = outputCount > 0 and outputCount or requestedCount,
      reason = nil,
      missing = {}
    }
  end

  -- partial / missing materials
  if output ~= nil and not isEmptyTable(missing) then
    return {
      status = "partial",
      requested = requestedCount,
      accepted = outputCount,
      reason = "missing_items",
      missing = missingList
    }
  end

  -- no usable output
  return {
    status = "failed",
    requested = requestedCount,
    accepted = 0,
    reason = debugMsg or "no_output",
    missing = missingList
  }
end

-- =========================
-- Craft execution
-- =========================
local function performCraft(itemName, count)
  local ok, task = pcall(function()
    return ae.craftItem({
      name = itemName,
      count = count
    })
  end)

  if not ok then
    return {
      status = "failed",
      requested = count,
      accepted = 0,
      reason = tostring(task),
      missing = {}
    }
  end

  if not task then
    return {
      status = "failed",
      requested = count,
      accepted = 0,
      reason = "craft_task_nil",
      missing = {}
    }
  end

  return inspectCraftTask(task, count)
end

-- =========================
-- Handle craft command
-- =========================
local function handleCraftCommand(cmd)
  local payload = cmd.payload or {}
  local item = payload.item
  local count = tonumber(payload.count) or 1

  if not item then
    finishCommand({
      id = cmd.id,
      status = "failed",
      requested = count,
      accepted = 0,
      reason = "missing_item",
      missing = {}
    })
    return
  end

  -- ověření, že na item existuje pattern
  local hasPattern = false
  local craftable = ae.getCraftableItems()

  for _, it in ipairs(craftable) do
    if it.name == item then
      hasPattern = true
      break
    end
  end

  if not hasPattern then
    finishCommand({
      id = cmd.id,
      status = "failed",
      requested = count,
      accepted = 0,
      reason = "no_pattern",
      missing = {}
    })
    return
  end

  local okAck = ackCommand(cmd.id)
  if not okAck then
    log("ACK failed for", cmd.id)
  end

  local result = performCraft(item, count)
  result.id = cmd.id

  finishCommand(result)
end

-- =========================
-- Websocket listener loop
-- =========================
local function commandLoop()
  while true do
    log("Connecting WS...")
    local ws, err = http.websocket(WS_URL)

    if not ws then
      log("WS connect failed:", err)
      sleep(3)
    else
      log("WS connected")

      while true do
        local msg = ws.receive()
        if not msg then
          log("WS disconnected")
          break
        end

        local ok, data = pcall(textutils.unserialiseJSON, msg)

        if not ok or not data then
          log("Invalid WS JSON:", msg)
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
