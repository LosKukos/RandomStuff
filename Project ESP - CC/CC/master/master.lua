local CFG = require("config")
local util = require("util")
local esp = require("esp")
local me = require("me")
local factory = require("factory")
local orders = require("orders")

local function printReason(result)
  local reason = result.reason

  if not reason and type(result.precheck) == "table" and result.precheck.reason then
    reason = result.precheck
  elseif not reason and type(result.export) == "table" and result.export.reason then
    reason = result.export
  elseif not reason and type(result.buffer) == "table" and result.buffer.reason then
    reason = result.buffer
  elseif not reason and type(result.depot) == "table" and result.depot.reason then
    reason = result.depot
  end

  print("Reason:")
  if reason == nil then
    print("unknown")
  elseif type(reason) == "table" then
    util.dump(reason)
  else
    print(tostring(reason))
  end
end

local function printPackageResult(ok, result)
  util.printHeader(ok and ("PACKAGE OK " .. tostring(result.packageId)) or ("PACKAGE FAIL " .. tostring(result.packageId)))

  print("Filter:")
  util.dump(result.filter)

  if result.precheck then
    print("Precheck:")
    util.dump(result.precheck)
  end

  if result.export then
    print("Export:")
    util.dump(result.export)
  end

  if result.buffer then
    print("Buffer:")
    util.dump(result.buffer)
  end

  if result.depot then
    print("Depot:")
    util.dump({
      address = result.depot.address,
      contents = result.depot.contents
    })
  end

  if not ok then
    printReason(result)
  end
end

local function packOnePackage(job)
  local result = {
    packageId = job.packageId,
    orderId = job.orderId,
    filter = job.filter,
    address = job.packageId
  }

  do
    local ok, data = me.precheck(job.filter)
    result.precheck = data
    if not ok then
      result.reason = data
      return false, result
    end
  end

  do
    local ok, addrRes = factory.setPackagerAddress(job.packageId)
    result.packagerAddress = addrRes
    if not ok then
      result.reason = addrRes
      return false, result
    end
  end

  do
    local ok, data = me.exportIfEnough(job.filter)
    result.export = data
    if not ok then
      result.reason = data
      return false, result
    end
  end

  do
    local ok, data = factory.waitForCreateFeed(job.filter)
    result.buffer = data
    if not ok then
      result.reason = data
      return false, result
    end
  end

  do
    local ok, data = factory.makePackage()
    result.makePackage = data
    if not ok then
      result.reason = data
      return false, result
    end
  end

  do
    local ok, data = factory.readDepotPackage(job.packageId)
    result.depot = data
    if not ok then
      result.reason = data
      return false, result
    end
  end

  do
    local ok, data = factory.releaseFromDepot()
    result.release = data or true
    if not ok then
      result.reason = data
      return false, result
    end
  end

  return true, result
end

local function processOrder(order)
  util.printHeader("PROCESS ORDER " .. tostring(order.orderId))
  util.dump(order)

  esp.updateOrder(order.orderId, "processing", {})

  local jobs = orders.splitOrderToPackages(order)
  local packedIds = {}

  for _, job in ipairs(jobs) do
    local ok, result = packOnePackage(job)
    printPackageResult(ok, result)

    if not ok then
      esp.updateOrder(order.orderId, "failed", {
        packageId = job.packageId,
        reason = result.reason
      })
      return false, result
    end

    local registerOk, registerRes = esp.registerPackage({
      packageId = job.packageId,
      orderId = job.orderId,
      destination = job.destination,
      deliveryMode = job.deliveryMode,
      recipient = job.recipient,
      address = job.packageId,
      contents = result.depot and result.depot.contents or {},
      filter = job.filter
    })

    if not registerOk or not registerRes or registerRes.ok == false then
      esp.updateOrder(order.orderId, "failed", {
        packageId = job.packageId,
        reason = "package_register_failed",
        detail = registerRes or registerOk
      })
      return false, {
        reason = "package_register_failed",
        detail = registerRes
      }
    end

    table.insert(packedIds, job.packageId)
  end

  esp.updateOrder(order.orderId, "packed", {
    packages = packedIds
  })

  return true, {
    packages = packedIds
  }
end

local function fetchPendingOrders()
  local ok, res = esp.getPendingOrders()
  if not ok then
    return false, res
  end

  if not res or res.ok == false or not res.data or type(res.data.orders) ~= "table" then
    return false, res
  end

  return true, res.data.orders
end

local function main()
  util.printHeader("MASTER START")

  while true do
    local ok, pending = fetchPendingOrders()

    if not ok then
      print("[ESP] pending fetch failed")
      util.dump(pending)
      sleep(CFG.idleSleep)
    else
      if #pending == 0 then
        print("[MASTER] no pending orders")
        sleep(CFG.idleSleep)
      else
        for _, order in ipairs(pending) do
          local orderOk, orderRes = processOrder(order)
          if not orderOk then
            print("[MASTER] order failed")
            util.dump(orderRes)
          else
            print("[MASTER] order packed OK")
            util.dump(orderRes)
          end
        end
      end
    end
  end
end

main()
