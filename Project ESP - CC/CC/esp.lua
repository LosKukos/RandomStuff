local CFG = require("config")

local M = {}

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

function M.getPendingOrders()
  return postJson("/api/orders/pending", {})
end

function M.updateOrder(orderId, status, meta)
  return postJson("/api/orders/update", {
    orderId = orderId,
    status = status,
    meta = meta or {}
  })
end

function M.registerPackage(pkg)
  return postJson("/api/package/register", pkg)
end

return M
