local M = {}

function M.dump(v)
  print(textutils.serialise(v))
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
