local CFG = require("config")

local M = {}

local function postJson(path, payload)
  local url = CFG.espBase .. path
  local body = textutils.serialiseJSON(payload)

  local res, err = http.post(url, body, {
    ["Content-Type"] = "application/json"
  })

  if not res then
    return false, {
      reason = "http_post_failed",
      error = err,
      url = url
    }
  end

  local raw = res.readAll()
  res.close()

  local ok, data = pcall(textutils.unserialiseJSON, raw)
  if not ok or type(data) ~= "table" then
    return false, {
      reason = "invalid_json_response",
      raw = raw,
      url = url
    }
  end

  if data.ok == false then
    return false, data
  end

  return true, data
end

function M.registerNode(nodeId)
  local payload = {
    nodeName = CFG.nodeName
  }

  if nodeId and nodeId ~= "" then
    payload.nodeId = nodeId
  end

  return postJson("/api/node/register", payload)
end

function M.heartbeatNode(nodeId)
  return postJson("/api/node/heartbeat", {
    nodeId = nodeId,
    nodeName = CFG.nodeName
  })
end

function M.reportPackagePass(nodeId, packageId)
  return postJson("/api/package/event", {
    packageId = packageId,
    nodeId = nodeId,
    event = CFG.event or "pass"
  })
end

return M
