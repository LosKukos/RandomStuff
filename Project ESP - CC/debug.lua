local ae = peripheral.wrap("me_bridge_0")

local f = fs.open("craft_debug.txt", "w")

local function log(...)
  local parts = {}
  for i = 1, select("#", ...) do
    local v = select(i, ...)
    parts[#parts + 1] = tostring(v)
  end
  f.writeLine(table.concat(parts, " "))
end

local function dumpValue(name, value)
  log(name .. " type:", type(value))

  if type(value) == "table" then
    local ok, txt = pcall(textutils.serialise, value)
    if ok then
      log(name .. " value:", txt)
    else
      log(name .. " value: <cannot serialise>")
    end
  else
    log(name .. " value:", tostring(value))
  end
end

local ok, task = pcall(function()
  return ae.craftItem({ name = "minecraft:iron_ingot", count = 64 })
end)

log("pcall ok:", ok)
dumpValue("task", task)

if ok and task then
  if task.isDone then log("isDone:", task.isDone()) end
  if task.isCanceled then log("isCanceled:", task.isCanceled()) end
  if task.hasErrorOccurred then log("hasErrorOccurred:", task.hasErrorOccurred()) end
  if task.isCalculationNotSuccessful then log("isCalculationNotSuccessful:", task.isCalculationNotSuccessful()) end
  if task.isCalculationStarted then log("isCalculationStarted:", task.isCalculationStarted()) end
  if task.isCraftingStarted then log("isCraftingStarted:", task.isCraftingStarted()) end
  if task.getDebugMessage then log("getDebugMessage:", task.getDebugMessage()) end

  sleep(0.5)

  if task.getRequestedItem then
    dumpValue("requested", task.getRequestedItem())
  end

  if task.getFinalOutput then
    dumpValue("output", task.getFinalOutput())
  end

  if task.getMissingItems then
    dumpValue("missing", task.getMissingItems())
  end
end

f.close()
print("saved to craft_debug.txt")
