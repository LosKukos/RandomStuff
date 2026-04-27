local CFG = require("config")
local util = require("util")
local esp = require("esp")
local scanner = require("scanner")

local state = util.readJsonFile(CFG.stateFile) or {}
local lastSeen = {}

local function saveState()
  util.writeJsonFile(CFG.stateFile, state)
end

local function ensureRegistered()
  local knownId = state.nodeId
  local ok, res = esp.registerNode(knownId)

  if not ok then
    print("[NODE] registration failed")
    util.dump(res)
    return false
  end

  local data = res.data or {}
  if not data.nodeId then
    print("[NODE] registration response missing nodeId")
    util.dump(res)
    return false
  end

  state.nodeId = data.nodeId
  state.nodeName = data.nodeName or CFG.nodeName
  saveState()

  print("[NODE] registered as " .. tostring(state.nodeId) .. " / " .. tostring(state.nodeName))
  return true
end

local function heartbeatOnce()
  if not state.nodeId then return false end

  local ok, res = esp.heartbeatNode(state.nodeId)
  if ok then
    local data = res.data or {}
    print("[NODE] heartbeat OK " .. tostring(state.nodeId) .. " @ " .. tostring(data.lastSeenLabel or "time_unknown"))
    return true
  end

  print("[NODE] heartbeat failed, trying re-register")
  util.dump(res)
  return ensureRegistered()
end

local function shouldReport(packageId)
  local now = util.nowClock()
  local last = lastSeen[packageId]

  if last and (now - last) < CFG.debounceSeconds then
    if CFG.verboseDebounce then
      print("[NODE] debounce skip " .. tostring(packageId))
    end
    return false
  end

  lastSeen[packageId] = now
  return true
end

local function releaseIfConfigured()
  if not CFG.releaseEnabled then
    return
  end

  util.pulse(CFG.releaseSide, CFG.releasePulse)
end

local function report(packageId)
  if not state.nodeId then
    print("[NODE] no nodeId, attempting registration")
    if not ensureRegistered() then return false end
  end

  local ok, res = esp.reportPackagePass(state.nodeId, packageId)

  if ok then
    local data = res.data or {}
    local timeLabel = data.timeLabel or data.timeIso or data.timeMs or "time_unknown"
    print("[NODE] " .. tostring(packageId) .. " seen at " .. tostring(state.nodeId) .. " @ " .. tostring(timeLabel))
  else
    print("[NODE] report failed for " .. tostring(packageId))
    util.dump(res)

    if type(res) == "table" and res.error == "node_not_found" then
      state.nodeId = nil
      saveState()
      ensureRegistered()
    end
  end

  return ok, res
end

local function scanLoop()
  while true do
    local found, data = scanner.scan()

    if found and data and data.packageId then
      local packageId = data.packageId

      if shouldReport(packageId) then
        report(packageId)
        releaseIfConfigured()
      end
    end

    sleep(CFG.poll)
  end
end

local function heartbeatLoop()
  while true do
    heartbeatOnce()
    sleep(CFG.heartbeatSeconds)
  end
end

util.printHeader("CC PACKAGE NODE")
print("Name: " .. tostring(CFG.nodeName))
print("Scanner: " .. tostring(CFG.scanner))
print("ESP: " .. tostring(CFG.espBase))
print("Event: " .. tostring(CFG.event or "pass"))
print("Release: " .. tostring(CFG.releaseEnabled))

while not ensureRegistered() do
  print("[NODE] retrying registration in 5s")
  sleep(5)
end

parallel.waitForAny(scanLoop, heartbeatLoop)
