local CFG = require("config")
local util = require("util")

local M = {}

local me = util.wrapPeripheral(CFG.meBridge, "ME Bridge")

function M.matchesItem(item, filter)
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

function M.findMatchingItem(filter)
  local ok, items = util.safeCall(me.getItems)
  if not ok or type(items) ~= "table" then
    return nil, "getItems_failed"
  end

  for _, item in ipairs(items) do
    if M.matchesItem(item, filter) then
      return item
    end
  end

  return nil, "item_not_found"
end

function M.getAvailableCount(filter)
  local item, err = M.findMatchingItem(filter)
  if not item then
    return 0, err, nil
  end

  return item.amount or item.count or 0, nil, item
end

function M.precheck(filter)
  local available, err, meItem = M.getAvailableCount(filter)

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

function M.exportIfEnough(filter)
  local requested = tonumber(filter.count) or 1
  local available, err, meItem = M.getAvailableCount(filter)

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

return M
