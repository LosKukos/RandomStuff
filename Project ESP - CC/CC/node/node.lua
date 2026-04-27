local CFG = require("config")
local util = require("util")
local esp = require("esp")
local scanner = require("scanner")

local lastSeen = {}

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
  local ok, res = esp.reportPackagePass(packageId)

  if ok then
    local data = res.data or {}
    local timeLabel = data.timeLabel or data.isoTime or data.timeMs or "time_unknown"
    print("[NODE] " .. tostring(packageId) .. " seen at " .. tostring(CFG.nodeId) .. " @ " .. tostring(timeLabel))
  else
    print("[NODE] report failed for " .. tostring(packageId))
    util.dump(res)
  end

  return ok, res
end

local function loop()
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

util.printHeader("CC PACKAGE NODE")
print("Node: " .. tostring(CFG.nodeId) .. " / " .. tostring(CFG.nodeName))
print("Scanner: " .. tostring(CFG.scanner))
print("ESP: " .. tostring(CFG.espBase))
print("Event: " .. tostring(CFG.event or "pass"))
print("Release: " .. tostring(CFG.releaseEnabled))

loop()
