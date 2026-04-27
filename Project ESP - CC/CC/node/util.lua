local M = {}

function M.dump(v)
  print(textutils.serialise(v))
end

function M.printHeader(title)
  print("")
  print(("="):rep(46))
  print(title)
  print(("="):rep(46))
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
    error("Peripheral not found for " .. label .. ": " .. tostring(name))
  end
  return p
end

function M.pulse(side, length)
  redstone.setOutput(side, true)
  sleep(length)
  redstone.setOutput(side, false)
end

function M.nowClock()
  return os.clock()
end

return M
