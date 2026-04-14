local ae = peripheral.wrap("me_bridge_0")

local task = ae.craftItem({ name = "minecraft:iron_ingot", count = 64 })

print("isDone:", task.isDone())
print("isCanceled:", task.isCanceled())
print("hasErrorOccurred:", task.hasErrorOccurred())
print("isCalculationNotSuccessful:", task.isCalculationNotSuccessful())
print("debug:", task.getDebugMessage())

local req = task.getRequestedItem()
print("requested type:", type(req))
if type(req) == "table" then
  print("requested:", textutils.serialise(req))
else
  print("requested:", tostring(req))
end

local out = task.getFinalOutput()
print("output type:", type(out))
if type(out) == "table" then
  print("output:", textutils.serialise(out))
else
  print("output:", tostring(out))
end

local missing = task.getMissingItems()
print("missing type:", type(missing))
if type(missing) == "table" then
  print("missing:", textutils.serialise(missing))
else
  print("missing:", tostring(missing))
end
