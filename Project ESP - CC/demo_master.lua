-- master_v6.lua
-- MASTER V6
-- order queue z ESP + packaging pipeline
--
-- Flow:
-- 1) stahni pending orders z ESP
-- 2) rozsekaj itemy na balicky po 64
-- 3) kazdy balik zabal
-- 4) registruj package do ESP
-- 5) update order status

-- ==================================================
-- CONFIG
-- ==================================================
local CFG = {
  -- peripherals
  meBridge = "me_bridge_0",                  -- peripheral name/side
  packager = "Create_Packager_0",                   -- peripheral name/side
  bufferChest = "minecraft:barrel_0",   -- jmeno buffer chest
  depot = "create:depot_2",                      -- peripheral name/side depotu
  bridgeChestDirection = "right",         -- smer exportu z ME bridge do bridge chest
  armSetRedstoneSide = "back",          -- SET vstup SR latch

  -- esp
  espBase = "http://10.0.1.17",

  -- timing
  bufferAppearTimeout = 10,
  depotTimeout = 10,
  depotClearTimeout = 5,
  settleDelay = 0.5,
  setPulse = 0.15,
  poll = 0.1,
  idleSleep = 3,
}

-- ==================================================
-- UTILS
-- ==================================================
local function dump(v)
  print(textutils.serialise(v))
end

local function printHeader(title)
  print("")
  print(("="):rep(56))
  print(title)
  print(("="):rep(56))
end

local function safeCall(fn, ...)
  local ok, a, b, c, d = pcall(fn, ...)
  if not ok then
    return false, a, b, c, d
  end
  return true, a, b, c, d
end

local function wrapPeripheral(name, label)
  local p = peripheral.wrap(name)
  if not p then
    error("Nenalezena periferie pro " .. label .. ": " .. tostring(name))
  end
  return p
end

local function pulseSet(side, pulseLength)
  redstone.setOutput(side, true)
  sleep(pulseLength)
  redstone.setOutput(side, false)
end

local function makePackageId(orderId, idx)
  return string.format("%s|P%02d", orderId, idx)
end

-- ==================================================
-- INIT
-- ==================================================
local me = wrapPeripheral(CFG.meBridge, "ME Bridge")
local packager = wrapPeripheral(CFG.packager, "Packager")
local bufferChest = wrapPeripheral(CFG.bufferChest, "Buffer Chest")
local depot = wrapPeripheral(CFG.depot, "Depot")

redstone.setOutput(CFG.armSetRedstoneSide, false)

-- ==================================================
-- HTTP / ESP
-- ==================================================
local function postJson(path, payload)
  local url = CFG.espBase .. path
  local body = textutils.serialiseJSON(payload)

  local res, err = http.post(url, body, {
    ["Content-Type"] = "application/json"
  })

  if not res then
    return false, err
  end

  local raw = res.readAll()
  res.close()

  local ok, data = pcall(textutils.unserialiseJSON, raw)
  if not ok then
    return false, "invalid_json_response"
  end

  return true, data
end

local function espGetPendingOrders()
  return postJson("/api/orders/pending", {})
end

local function espUpdateOrder(orderId, status, meta)
  return postJson("/api/orders/update", {
    orderId = orderId,
    status = status,
    meta = meta or {}
  })
end

local function espRegisterPackage(pkg)
  return postJson("/api/package/register", pkg)
end

-- ==================================================
-- MATCHING / ME
-- ==================================================
local function matchesItem(item, filter)
  if filter.name and item.name ~= filter.name then
    return false
  end

  if filter.nbt and item.nbt ~= filter.nbt then
    return false
  end

  if filter.fingerprint and item.fingerprint ~= filter.fingerprint then
    return false
  end

  return true
end

local function findMatchingMEItem(filter)
  local ok, items = safeCall(me.getItems)
  if not ok or type(items) ~= "table" then
    return nil, "getItems_failed"
  end

  for _, item in ipairs(items) do
    if matchesItem(item, filter) then
      return item
    end
  end

  return nil, "item_not_found"
end

local function getAvailableCount(filter)
  local item, err = findMatchingMEItem(filter)
  if not item then
    return 0, err, nil
  end

  return item.amount or item.count or 0, nil, item
end

local function precheckME(filter)
  local available, err, meItem = getAvailableCount(filter)

  if available < (filter.count or 1) then
    return false, {
      reason = "not_enough_items",
      requested = filter.count,
      available = available,
      matchError = err,
      meItem = meItem
    }
  end

  return true, {
    available = available,
    meItem = meItem
  }
end

local function exportIfEnough(filter)
  local requested = tonumber(filter.count) or 1
  local available, err, meItem = getAvailableCount(filter)

  if available < requested then
    return false, {
      reason = "not_enough_items",
      requested = requested,
      available = available,
      matchError = err,
      meItem = meItem
    }
  end

  local moved, exportErr = me.exportItem(filter, CFG.bridgeChestDirection)

  if not moved or moved < requested then
    return false, {
      reason = "unexpected_partial_or_export_fail",
      requested = requested,
      moved = moved or 0,
      err = exportErr
    }
  end

  return true, {
    moved = moved
  }
end

-- ==================================================
-- BUFFER / DEPOT
-- ==================================================
local function getBufferCount(filter)
  local ok, items = safeCall(bufferChest.list)
  if not ok or type(items) ~= "table" then
    return 0
  end

  local total = 0
  for _, item in pairs(items) do
    if item.name == filter.name then
      if filter.nbt then
        if item.nbt == filter.nbt then
          total = total + (item.count or 0)
        end
      else
        total = total + (item.count or 0)
      end
    end
  end

  return total
end

local function waitForBufferAtLeast(filter, wantedCount, timeoutSec)
  local deadline = os.clock() + timeoutSec

  while os.clock() < deadline do
    local count = getBufferCount(filter)
    if count >= wantedCount then
      return true, count
    end
    sleep(CFG.poll)
  end

  return false, getBufferCount(filter)
end

local function waitForDepotPackage(timeoutSec)
  local deadline = os.clock() + timeoutSec

  while os.clock() < deadline do
    local ok, item = safeCall(depot.getItemDetail, 1)
    if ok and item and item.package then
      return true, item
    end
    sleep(CFG.poll)
  end

  return false, nil
end

local function waitForDepotEmpty(timeoutSec)
  local deadline = os.clock() + timeoutSec

  while os.clock() < deadline do
    local ok, item = safeCall(depot.getItemDetail, 1)
    if ok and not item then
      return true
    end
    sleep(CFG.poll)
  end

  return false
end

local function waitForCreateFeed(filter)
  local okAppear, actualAppear = waitForBufferAtLeast(filter, filter.count, CFG.bufferAppearTimeout)
  if not okAppear then
    return false, {
      reason = "buffer_appear_timeout",
      actual = actualAppear,
      expected = filter.count
    }
  end

  sleep(CFG.settleDelay)

  return true, {
    appeared = actualAppear,
    settled = true
  }
end

-- ==================================================
-- PACKAGER / PACKAGE
-- ==================================================
local function setPackagerAddress(address)
  local ok, res = safeCall(packager.setAddress, address)
  if not ok then
    return false, "setAddress_failed: " .. tostring(res)
  end

  local ok2, currentAddress = safeCall(packager.getAddress)
  if not ok2 then
    return false, "getAddress_failed: " .. tostring(currentAddress)
  end

  if currentAddress ~= address then
    return false, "address_mismatch_on_packager"
  end

  return true, currentAddress
end

local function makePackage()
  local ok, res = safeCall(packager.makePackage, true)
  if not ok then
    return false, "makePackage_failed: " .. tostring(res)
  end
  return true, res
end

local function readDepotPackage(expectedAddress)
  local ok, item = waitForDepotPackage(CFG.depotTimeout)
  if not ok then
    return false, "package_not_found_on_depot"
  end

  if not item.package then
    return false, "depot_item_has_no_package_api"
  end

  local pkg = item.package

  local okAddr, addr = safeCall(function()
    return pkg:getAddress()
  end)

  if not okAddr then
    return false, "pkg_getAddress_failed: " .. tostring(addr)
  end

  if addr ~= expectedAddress then
    return false, "depot_address_mismatch: expected=" .. tostring(expectedAddress) .. " got=" .. tostring(addr)
  end

  local okList, contents = safeCall(function()
    return pkg:list()
  end)

  if not okList then
    contents = nil
  end

  return true, {
    address = addr,
    contents = contents,
    rawItem = item
  }
end

local function releaseFromDepot()
  pulseSet(CFG.armSetRedstoneSide, CFG.setPulse)

  local cleared = waitForDepotEmpty(CFG.depotClearTimeout)
  if not cleared then
    return false, "depot_not_cleared_after_release"
  end

  return true
end

-- ==================================================
-- ORDER SPLIT
-- ==================================================
local function splitOrderToPackages(order)
  local out = {}
  local idx = 1

  for _, item in ipairs(order.items or {}) do
    local remaining = tonumber(item.count) or 0

    while remaining > 0 do
      local chunk = math.min(64, remaining)

      local filter = {
        name = item.name,
        count = chunk
      }

      if item.nbt then
        filter.nbt = item.nbt
      end

      if item.fingerprint then
        filter.fingerprint = item.fingerprint
      end

      table.insert(out, {
        packageId = makePackageId(order.orderId, idx),
        orderId = order.orderId,
        destination = order.destination,
        deliveryMode = order.deliveryMode,
        recipient = order.recipient,
        filter = filter
      })

      remaining = remaining - chunk
      idx = idx + 1
    end
  end

  return out
end

-- ==================================================
-- CORE PACK FLOW
-- ==================================================
local function packOnePackage(job)
  local result = {
    packageId = job.packageId,
    orderId = job.orderId,
    filter = job.filter,
    address = job.packageId
  }

  do
    local ok, data = precheckME(job.filter)
    result.precheck = data
    if not ok then
      result.reason = data
      return false, result
    end
  end

  do
    local ok, addrRes = setPackagerAddress(job.packageId)
    result.packagerAddress = addrRes
    if not ok then
      result.reason = addrRes
      return false, result
    end
  end

  do
    local ok, data = exportIfEnough(job.filter)
    result.export = data
    if not ok then
      result.reason = data
      return false, result
    end
  end

  do
    local ok, data = waitForCreateFeed(job.filter)
    result.buffer = data
    if not ok then
      result.reason = data
      return false, result
    end
  end

  do
    local ok, data = makePackage()
    result.makePackage = data
    if not ok then
      result.reason = data
      return false, result
    end
  end

  do
    local ok, data = readDepotPackage(job.packageId)
    result.depot = data
    if not ok then
      result.reason = data
      return false, result
    end
  end

  do
    local ok, data = releaseFromDepot()
    result.release = data or true
    if not ok then
      result.reason = data
      return false, result
    end
  end

  return true, result
end

-- ==================================================
-- RESULT PRINT
-- ==================================================
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
    dump(reason)
  else
    print(tostring(reason))
  end
end

local function printPackageResult(ok, result)
  printHeader(ok and ("PACKAGE OK " .. tostring(result.packageId)) or ("PACKAGE FAIL " .. tostring(result.packageId)))

  print("Filter:")
  dump(result.filter)

  if result.precheck then
    print("Precheck:")
    dump(result.precheck)
  end

  if result.export then
    print("Export:")
    dump(result.export)
  end

  if result.buffer then
    print("Buffer:")
    dump(result.buffer)
  end

  if result.depot then
    print("Depot:")
    dump({
      address = result.depot.address,
      contents = result.depot.contents
    })
  end

  if not ok then
    printReason(result)
  end
end

-- ==================================================
-- PROCESS ORDER
-- ==================================================
local function processOrder(order)
  printHeader("PROCESS ORDER " .. tostring(order.orderId))
  dump(order)

  espUpdateOrder(order.orderId, "processing", {})

  local jobs = splitOrderToPackages(order)
  local packedIds = {}

  for _, job in ipairs(jobs) do
    local ok, result = packOnePackage(job)
    printPackageResult(ok, result)

    if not ok then
      espUpdateOrder(order.orderId, "failed", {
        packageId = job.packageId,
        reason = result.reason
      })
      return false, result
    end

    local registerOk, registerRes = espRegisterPackage({
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
      espUpdateOrder(order.orderId, "failed", {
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

  espUpdateOrder(order.orderId, "packed", {
    packages = packedIds
  })

  return true, {
    packages = packedIds
  }
end

-- ==================================================
-- MAIN LOOP
-- ==================================================
local function fetchPendingOrders()
  local ok, res = espGetPendingOrders()
  if not ok then
    return false, res
  end

  if not res or res.ok == false or not res.data or type(res.data.orders) ~= "table" then
    return false, res
  end

  return true, res.data.orders
end

local function main()
  printHeader("MASTER V6 START")

  while true do
    local ok, orders = fetchPendingOrders()

    if not ok then
      print("[ESP] pending fetch failed")
      dump(orders)
      sleep(CFG.idleSleep)
    else
      if #orders == 0 then
        print("[MASTER] no pending orders")
        sleep(CFG.idleSleep)
      else
        for _, order in ipairs(orders) do
          local orderOk, orderRes = processOrder(order)
          if not orderOk then
            print("[MASTER] order failed")
            dump(orderRes)
          else
            print("[MASTER] order packed OK")
            dump(orderRes)
          end
        end
      end
    end
  end
end

main()
