local CFG = require("config")
local util = require("util")

local M = {}

local scanner = util.wrapPeripheral(CFG.scanner, "Package Scanner")

local function readFromItemDetail()
  local ok, item = util.safeCall(scanner.getItemDetail, 1)
  if not ok or not item then
    return false, nil
  end

  if item.package then
    local okAddr, addr = util.safeCall(function()
      return item.package:getAddress()
    end)

    if okAddr and addr and addr ~= "" then
      return true, {
        packageId = addr,
        rawItem = item,
        source = "getItemDetail.package"
      }
    end
  end

  return false, nil
end

local function readFromListFallback()
  local ok, items = util.safeCall(scanner.list)
  if not ok or type(items) ~= "table" then
    return false, nil
  end

  for slot, item in pairs(items) do
    if item and item.package then
      local okAddr, addr = util.safeCall(function()
        return item.package:getAddress()
      end)

      if okAddr and addr and addr ~= "" then
        return true, {
          packageId = addr,
          slot = slot,
          rawItem = item,
          source = "list.package"
        }
      end
    end
  end

  return false, nil
end

function M.scan()
  local ok, data = readFromItemDetail()
  if ok then return true, data end

  ok, data = readFromListFallback()
  if ok then return true, data end

  return false, nil
end

return M
