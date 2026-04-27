local M = {}

function M.dump(v)
  print(textutils.serialise(v))
end

function M.printHeader(title)
  print("")
  print(("="):rep(44))
  print(title)
  print(("="):rep(44))
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

function M.pulse(side, seconds)
  redstone.setOutput(side, true)
  sleep(seconds or 0.15)
  redstone.setOutput(side, false)
end

function M.nowClock()
  return os.clock()
end

function M.readJsonFile(path)
  if not fs.exists(path) then
    return nil
  end

  local h = fs.open(path, "r")
  if not h then return nil end

  local raw = h.readAll()
  h.close()

  if not raw or raw == "" then return nil end

  local ok, data = pcall(textutils.unserialiseJSON, raw)
  if not ok then return nil end
  return data
end

function M.writeJsonFile(path, data)
  local h = fs.open(path, "w")
  if not h then return false end

  h.write(textutils.serialiseJSON(data))
  h.close()
  return true
end

return M
