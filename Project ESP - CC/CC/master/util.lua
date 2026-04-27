local M = {}

local function sanitizeForSerialise(value, seen)
  local t = type(value)

  if t == "table" then
    if seen[value] then
      return "<repeated>"
    end

    seen[value] = true

    local out = {}

    for k, v in pairs(value) do
      local safeKey = k

      if type(k) == "table" then
        safeKey = tostring(k)
      elseif type(k) == "function" then
        safeKey = "<function-key>"
      elseif type(k) == "userdata" or type(k) == "thread" then
        safeKey = tostring(k)
      end

      out[safeKey] = sanitizeForSerialise(v, seen)
    end

    return out
  end

  if t == "function" then
    return "<function>"
  end

  if t == "userdata" or t == "thread" then
    return tostring(value)
  end

  return value
end

function M.dump(v)
  local ok, serialized = pcall(textutils.serialise, v)

  if ok then
    print(serialized)
    return
  end

  local safe = sanitizeForSerialise(v, {})
  local okSafe, serializedSafe = pcall(textutils.serialise, safe)

  if okSafe then
    print(serializedSafe)
  else
    print(tostring(v))
  end
end

function M.printHeader(title)
  print("")
  print(("="):rep(56))
  print(title)
  print(("="):rep(56))
end

function M.safeCall(fn, ...)
  local ok, a, b, c, d = pcall(fn, ...)
  if not ok then
    return false, a, b, c, d
  end
  return true, a, b, c, d
end

function M.wrapPeripheral(name, label)
  local p = peripheral.wrap(name)
  if not p then
    error("Nenalezena periferie pro " .. label .. ": " .. tostring(name))
  end
  return p
end

function M.pulseSet(side, pulseLength)
  redstone.setOutput(side, true)
  sleep(pulseLength)
  redstone.setOutput(side, false)
end

function M.makePackageId(orderId, idx)
  return string.format("%s|P%02d", orderId, idx)
end

return M
