local s = peripheral.wrap("TVUJ_SORTER")

local filters = s.getFilters()
local f = filters[1]

print(textutils.serialise(f))

s.removeFilter(0) -- nebo podle indexu, pokud sedí
sleep(0.5)

local ok, err = pcall(function()
  s.addFilter(f)
end)

print("addFilter:", ok, err)
print(textutils.serialise(s.getFilters()))
