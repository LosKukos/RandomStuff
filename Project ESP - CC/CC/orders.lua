local util = require("util")

local M = {}

function M.splitOrderToPackages(order)
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
        packageId = util.makePackageId(order.orderId, idx),
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

return M
