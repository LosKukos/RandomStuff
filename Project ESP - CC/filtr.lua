local SORTER_NAME = "mekanism:logistical_sorter_0"

local function log(...)
  print("[DEBUG]", ...)
end

local function clone(t)
  local out = {}
  for k, v in pairs(t) do
    out[k] = v
  end
  return out
end

local function escapePattern(s)
  return tostring(s):gsub("([^%w])", "%%%1")
end

local function safe(name, fn)
  local ok, res = pcall(fn)
  log(name, ok, res)
  return ok, res
end

-- ===================== START =====================

local s = peripheral.wrap(SORTER_NAME)
if not s then
  error("Sorter not found: " .. SORTER_NAME)
end

log("=== BASIC INFO ===")
safe("isSingle", function() return s.isSingle() end)
safe("getAutoMode", function() return s.getAutoMode() end)
safe("getRedstoneMode", function() return s.getRedstoneMode() end)

log("=== FILTER DUMP ===")
local filters = s.getFilters()
print(textutils.serialise(filters))

if #filters == 0 then
  error("No filters found. Create one manually first.")
end

local base = filters[1]

log("=== TEST addFilter(copy) ===")
local copy = clone(base)
safe("addFilter(copy)", function() return s.addFilter(copy) end)

log("=== TEST removeFilter(copy) ===")
safe("removeFilter(copy)", function() return s.removeFilter(copy) end)

log("=== TEST REMOVE ALL ===")
for _, f in ipairs(s.getFilters()) do
  safe("removeFilter loop", function() return s.removeFilter(f) end)
end

log("Filters after clear:")
print(textutils.serialise(s.getFilters()))

log("=== RESTORE BASE FILTER ===")
safe("addFilter(base)", function() return s.addFilter(base) end)

log("=== TEST MODIFY ADDRESS ===")

-- !!! UPRAV TADY ručně podle toho co máš v dumpu !!!
local OLD = "ORD48533099_1|P01"
local NEW = "TEST_REPLACED|P01"

local modified = clone(base)
modified.components = modified.components:gsub(escapePattern(OLD), NEW)

safe("addFilter(modified)", function() return s.addFilter(modified) end)

log("=== FINAL FILTERS ===")
print(textutils.serialise(s.getFilters()))

log("=== DONE ===")
