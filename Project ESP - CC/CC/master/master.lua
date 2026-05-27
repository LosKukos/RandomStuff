-- AXIS Master - factory order fulfillment
-- Full source: https://github.com/LosKukos/RandomStuff/tree/main/Project%20ESP%20-%20CC/CC/master/master.lua
-- Run via installer or copy manually.
local CFG = require("config")
local util = require("util")
local esp = require("esp")
local me = require("me")
local factory = require("factory")
local orders = require("orders")
local players = require("players")

local playerSyncInterval = 15  -- seconds
local lastPlayerSync = 0

local function processOrder(order)
  util.printHeader("PROCESS ORDER " .. tostring(order.orderId))
  esp.updateOrder(order.orderId, "processing", {})
  local jobs = orders.splitOrderToPackages(order)
  local packedIds = {}

  for _, job in ipairs(jobs) do
    -- precheck
    local okPre, prePdata = me.precheck(job.filter)
    if not okPre then
      esp.updateOrder(order.orderId, "failed", { packageId = job.packageId, reason = prePdata })
      return false
    end
    -- set packager address
    local okAddr, _ = factory.setPackagerAddress(job.packageId)
    if not okAddr then esp.updateOrder(order.orderId, "failed", {}); return false end
    -- export
    local okExp, _ = me.exportIfEnough(job.filter)
    if not okExp then esp.updateOrder(order.orderId, "failed", {}); return false end
    -- feed
    local okFeed, _ = factory.waitForCreateFeed(job.filter)
    if not okFeed then esp.updateOrder(order.orderId, "failed", {}); return false end
    -- make package
    local okMake, _ = factory.makePackage()
    if not okMake then esp.updateOrder(order.orderId, "failed", {}); return false end
    -- read depot
    local okDepot, depotData = factory.readDepotPackage(job.packageId)
    if not okDepot then esp.updateOrder(order.orderId, "failed", {}); return false end
    -- release
    local okRel, _ = factory.releaseFromDepot()
    if not okRel then esp.updateOrder(order.orderId, "failed", {}); return false end
    -- register package
    local regOk, _ = esp.registerPackage({
      packageId = job.packageId, orderId = job.orderId,
      destination = job.destination, deliveryMode = job.deliveryMode,
      recipient = job.recipient, address = job.packageId,
      contents = depotData and depotData.contents or {}, filter = job.filter
    })
    if not regOk then esp.updateOrder(order.orderId, "failed", {}); return false end
    table.insert(packedIds, job.packageId)
  end

  esp.updateOrder(order.orderId, "packed", { packages = packedIds })
  return true
end

local function main()
  util.printHeader("MASTER START")
  while true do
    local ok, res = esp.getPendingOrders()
    if not ok then
      print("[ESP] pending fetch failed"); util.dump(res); sleep(CFG.idleSleep)
    else
      local pending = (res and res.data and res.data.orders) or {}
      if #pending == 0 then
        print("[MASTER] no pending orders"); sleep(CFG.idleSleep)
      else
        for _, order in ipairs(pending) do
          local orderOk = processOrder(order)
          print(orderOk and "[MASTER] order packed OK" or "[MASTER] order failed")
        end
      end
    end

    -- Player sync
    local now = os.clock()
    if now - lastPlayerSync >= playerSyncInterval then
      local syncOk, syncMsg = players.sync()
      if not syncOk then
        print("[PLAYERS] sync failed: " .. tostring(syncMsg))
      end
      lastPlayerSync = now
    end
  end
end

main()
