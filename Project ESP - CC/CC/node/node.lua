-- AXIS Node - package checkpoint
-- Full source: https://github.com/LosKukos/RandomStuff/tree/main/Project%20ESP%20-%20CC/CC/node/node.lua
local CFG = require("config")
local util = require("util")
local esp = require("esp")
local scanner = require("scanner")

local state = util.readJsonFile(CFG.stateFile) or {}
local lastSeen = {}

local function saveState() util.writeJsonFile(CFG.stateFile, state) end

local function ensureRegistered()
  local ok, res = esp.registerNode(state.nodeId)
  if not ok then print("[NODE] registration failed"); util.dump(res); return false end
  local data = res.data or {}
  if not data.nodeId then print("[NODE] missing nodeId"); return false end
  state.nodeId = data.nodeId; state.nodeName = data.nodeName or CFG.nodeName
  saveState()
  print("[NODE] registered as " .. tostring(state.nodeId))
  return true
end

local function heartbeatOnce()
  if not state.nodeId then return false end
  local ok, res = esp.heartbeatNode(state.nodeId)
  if ok then print("[NODE] heartbeat OK"); return true end
  print("[NODE] heartbeat failed, re-registering"); util.dump(res)
  return ensureRegistered()
end

local function shouldReport(packageId)
  local now = util.nowClock()
  local last = lastSeen[packageId]
  if last and (now - last) < CFG.debounceSeconds then return false end
  lastSeen[packageId] = now
  return true
end

local function report(packageId)
  if not state.nodeId then
    print("[NODE] no nodeId"); if not ensureRegistered() then return false end
  end
  local ok, res = esp.reportPackagePass(state.nodeId, packageId)
  if ok then
    local data = res.data or {}
    print("[NODE] " .. tostring(packageId) .. " seen @ " .. tostring(data.timeLabel or "?"))
  else
    print("[NODE] report failed"); util.dump(res)
    if type(res) == "table" and res.error == "node_not_found" then
      state.nodeId = nil; saveState(); ensureRegistered()
    end
  end
  return ok
end

local function scanLoop()
  while true do
    local found, data = scanner.scan()
    if found and data and data.packageId then
      if shouldReport(data.packageId) then
        report(data.packageId)
        if CFG.releaseEnabled then util.pulse(CFG.releaseSide, CFG.releasePulse) end
      end
    end
    sleep(CFG.poll)
  end
end

local function heartbeatLoop()
  while true do heartbeatOnce(); sleep(CFG.heartbeatSeconds) end
end

util.printHeader("CC PACKAGE NODE")
print("Name: " .. tostring(CFG.nodeName))
print("ESP: " .. tostring(CFG.espBase))

while not ensureRegistered() do print("[NODE] retrying in 5s"); sleep(5) end

parallel.waitForAny(scanLoop, heartbeatLoop)
