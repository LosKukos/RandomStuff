local CFG = require("config")
local util = require("util")

local M = {}

local packager = util.wrapPeripheral(CFG.packager, "Packager")
local bufferChest = util.wrapPeripheral(CFG.bufferChest, "Buffer Chest")
local depot = util.wrapPeripheral(CFG.depot, "Depot")

redstone.setOutput(CFG.armSetRedstoneSide, false)

function M.getBufferCount(filter)
  local ok, items = util.safeCall(bufferChest.list)
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

function M.waitForBufferAtLeast(filter, wantedCount, timeoutSec)
  local deadline = os.clock() + timeoutSec

  while os.clock() < deadline do
    local count = M.getBufferCount(filter)
    if count >= wantedCount then
      return true, count
    end
    sleep(CFG.poll)
  end

  return false, M.getBufferCount(filter)
end

function M.waitForCreateFeed(filter)
  local okAppear, actualAppear = M.waitForBufferAtLeast(filter, filter.count, CFG.bufferAppearTimeout)
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

function M.setPackagerAddress(address)
  local ok, res = util.safeCall(packager.setAddress, address)
  if not ok then
    return false, "setAddress_failed: " .. tostring(res)
  end

  local ok2, currentAddress = util.safeCall(packager.getAddress)
  if not ok2 then
    return false, "getAddress_failed: " .. tostring(currentAddress)
  end

  if currentAddress ~= address then
    return false, "address_mismatch_on_packager"
  end

  return true, currentAddress
end

function M.makePackage()
  local ok, res = util.safeCall(packager.makePackage, true)
  if not ok then
    return false, "makePackage_failed: " .. tostring(res)
  end
  return true, res
end

function M.waitForDepotPackage(timeoutSec)
  local deadline = os.clock() + timeoutSec

  while os.clock() < deadline do
    local ok, item = util.safeCall(depot.getItemDetail, 1)
    if ok and item and item.package then
      return true, item
    end
    sleep(CFG.poll)
  end

  return false, nil
end

function M.waitForDepotEmpty(timeoutSec)
  local deadline = os.clock() + timeoutSec

  while os.clock() < deadline do
    local ok, item = util.safeCall(depot.getItemDetail, 1)
    if ok and not item then
      return true
    end
    sleep(CFG.poll)
  end

  return false
end

function M.readDepotPackage(expectedAddress)
  local ok, item = M.waitForDepotPackage(CFG.depotTimeout)
  if not ok then
    return false, "package_not_found_on_depot"
  end

  if not item.package then
    return false, "depot_item_has_no_package_api"
  end

  local pkg = item.package

  local okAddr, addr = util.safeCall(function()
    return pkg:getAddress()
  end)

  if not okAddr then
    return false, "pkg_getAddress_failed: " .. tostring(addr)
  end

  if addr ~= expectedAddress then
    return false, "depot_address_mismatch: expected=" .. tostring(expectedAddress) .. " got=" .. tostring(addr)
  end

  local okList, contents = util.safeCall(function()
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

function M.releaseFromDepot()
  util.pulseSet(CFG.armSetRedstoneSide, CFG.setPulse)

  local cleared = M.waitForDepotEmpty(CFG.depotClearTimeout)
  if not cleared then
    return false, "depot_not_cleared_after_release"
  end

  return true
end

return M
