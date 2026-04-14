-- master_poc_v3.lua
-- Refactorovana verze PoC master logiky
-- Bez partial pullu z ME
-- 1 balicek = 1 item type
-- Pouziva inspection depot a SR latch SET pulse pro arm release

-- ==================================================
-- CONFIG
-- ==================================================
local CFG = {
  meBridge = "right",                  -- peripheral name/side
  packager = "left",                   -- peripheral name/side
  bufferChest = "minecraft:chest_1",   -- jmeno buffer chest
  depot = "back",                      -- peripheral name/side depotu
  bridgeChestDirection = "up",         -- smer exportu z ME bridge do bridge chest
  armSetRedstoneSide = "top",          -- SET vstup SR latch

  bufferTimeout = 10,
  depotTimeout = 10,
  depotClearTimeout = 5,

  setPulse = 0.15,
  poll = 0.1,
}

-- ==================================================
-- UTILS
-- ==================================================
local function prompt(text, default)
  write(text)
  if default ~= nil then
    write(" [" .. tostring(default) .. "]")
  end
  write(": ")
  local v = read()
  if v == "" and default ~= nil then
    return default
  end
  return v
end

local function promptOptional(text)
  write(text .. " (leave empty if not needed): ")
  local v = read()
  if v == "" then
    return nil
  end
  return v
end

local function dump(v)
  print(textutils.serialise(v))
end

local function printHeader(title)
  print("")
  print(("="):rep(50))
  print(title)
  print(("="):rep(50))
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

-- ==================================================
-- INIT
-- ==================================================
local me = wrapPeripheral(CFG.meBridge, "ME Bridge")
local packager = wrapPeripheral(CFG.packager, "Packager")
local bufferChest = wrapPeripheral(CFG.bufferChest, "Buffer Chest")
local depot = wrapPeripheral(CFG.depot, "Depot")

redstone.setOutput(CFG.armSetRedstoneSide, false)

-- ==================================================
-- FILTER / MATCHING
-- ==================================================
local function buildFilterFromPrompt()
  local itemName = prompt("Item name", "minecraft:iron_ingot")
  local count = tonumber(prompt("Count", "16"))
  local address = prompt("Package address", "ORD0001|P01")
  local nbt = promptOptional("NBT")
  local fingerprint = promptOptional("Fingerprint")

  if not count or count < 1 then
    error("Neplatny count")
  end

  local filter = {
    name = itemName,
    count = count
  }

  if nbt then
    filter.nbt = nbt
  end

  if fingerprint then
    filter.fingerprint = fingerprint
  end

  return filter, address
end

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

-- ==================================================
-- BUFFER / DEPOT HELPERS
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

local function waitForBuffer(filter, wantedCount, timeoutSec)
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

-- ==================================================
-- PACKAGER / PACKAGE HELPERS
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
-- ME EXPORT WITHOUT PARTIAL PULL
-- ==================================================
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
-- CORE FLOW
-- ==================================================
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

local function waitForCreateFeed(filter)
  local ok, actual = waitForBuffer(filter, filter.count, CFG.bufferTimeout)
  if not ok then
    return false, {
      reason = "buffer_timeout",
      actual = actual,
      expected = filter.count
    }
  end

  return true, {
    actual = actual
  }
end

local function packOnePackage(filter, address)
  local result = {
    filter = filter,
    address = address
  }

  -- 1) pre-check ME
  do
    local ok, data = precheckME(filter)
    result.precheck = data
    if not ok then
      return false, result
    end
  end

  -- 2) set address
  do
    local ok, addrRes = setPackagerAddress(address)
    result.packagerAddress = addrRes
    if not ok then
      result.reason = addrRes
      return false, result
    end
  end

  -- 3) export from ME
  do
    local ok, data = exportIfEnough(filter)
    result.export = data
    if not ok then
      return false, result
    end
  end

  -- 4) wait buffer
  do
    local ok, data = waitForCreateFeed(filter)
    result.buffer = data
    if not ok then
      return false, result
    end
  end

  -- 5) wait for user confirmation that Create chain is ready
  print("")
  print("[INFO] Nech Create chain presunout item do packageru.")
  print("       Az bude ready k zabalení, stiskni ENTER.")
  read()

  -- 6) make package
  do
    local ok, data = makePackage()
    result.makePackage = data
    if not ok then
      result.reason = data
      return false, result
    end
  end

  -- 7) inspect on depot
  do
    local ok, data = readDepotPackage(address)
    result.depot = data
    if not ok then
      result.reason = data
      return false, result
    end
  end

  -- 8) release via SR latch
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
-- UI / MAIN LOOP
-- ==================================================
local function printResult(ok, result)
  printHeader(ok and "SUCCESS" or "FAIL")

  print("Address: " .. tostring(result.address))
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
    print("Reason:")
    print(tostring(result.reason or "unknown"))
  end
end

local function main()
  while true do
    printHeader("MASTER POC V3")

    local filter, address = buildFilterFromPrompt()

    print("")
    print("Input summary:")
    dump(filter)
    print("Address: " .. tostring(address))

    local ok, result = packOnePackage(filter, address)
    printResult(ok, result)

    print("")
    local again = prompt("Spustit další test? (y/n)", "y")
    if lower(again) ~= "y" then
      break
    end
  end
end

main()
