-- master_poc.lua
-- Proof of concept pro MASTER chain:
-- ME -> bridge chest -> buffer chest -> packager -> inspection depot -> arm -> output
--
-- CC:
-- - exportuje item z ME
-- - čeká na buffer
-- - nastaví address
-- - udělá balík
-- - přečte balík na depotu
-- - dá SET pulse do SR latch pro arm release

-- ==================================================
-- CONFIG
-- ==================================================
local ME_BRIDGE = "right"               -- peripheral name/side
local PACKAGER = "left"                 -- peripheral name/side
local BUFFER_CHEST = "minecraft:chest_1" -- jméno buffer chest, kterou čte CC
local DEPOT = "back"                    -- peripheral name/side depotu
local BRIDGE_CHEST_DIRECTION = "up"     -- směr exportu z ME bridge do bridge chest
local ARM_SET_REDSTONE_SIDE = "top"     -- redstone strana na SET vstup SR latch

-- Jak dlouho čekat na itemy / balík
local BUFFER_TIMEOUT = 10
local DEPOT_TIMEOUT = 10
local DEPOT_CLEAR_TIMEOUT = 5

-- Délka SET pulsu do latch
local SET_PULSE = 0.15

-- Polling interval
local POLL = 0.1

-- ==================================================
-- HELPERS
-- ==================================================
local function prompt(text, default)
  write(text)
  if default ~= nil then
    write(" [" .. tostring(default) .. "]")
  end
  write(": ")
  local v = read()
  if v == "" and default ~= nil then
    return default
  end
  return v
end

local function dump(v)
  print(textutils.serialise(v))
end

local function wrapPeripheral(name, label)
  local p = peripheral.wrap(name)
  if not p then
    error("Nenalezena periferie pro " .. label .. ": " .. tostring(name))
  end
  return p
end

local function safeCall(fn, ...)
  local ok, a, b, c, d = pcall(fn, ...)
  if not ok then
    return false, a, b, c, d
  end
  return true, a, b, c, d
end

local function pulseSet()
  redstone.setOutput(ARM_SET_REDSTONE_SIDE, true)
  sleep(SET_PULSE)
  redstone.setOutput(ARM_SET_REDSTONE_SIDE, false)
end

local function getBufferCount(inv, wantedName)
  local ok, items = safeCall(inv.list)
  if not ok or type(items) ~= "table" then
    return 0
  end

  local total = 0
  for _, item in pairs(items) do
    if item.name == wantedName then
      total = total + (item.count or 0)
    end
  end

  return total
end

local function waitForBuffer(inv, wantedName, wantedCount, timeoutSec)
  local deadline = os.clock() + timeoutSec

  while os.clock() < deadline do
    local count = getBufferCount(inv, wantedName)
    if count >= wantedCount then
      return true, count
    end
    sleep(POLL)
  end

  return false, getBufferCount(inv, wantedName)
end

local function waitForDepotPackage(depot, timeoutSec)
  local deadline = os.clock() + timeoutSec

  while os.clock() < deadline do
    local ok, item = safeCall(depot.getItemDetail, 1)
    if ok and item and item.package then
      return true, item
    end
    sleep(POLL)
  end

  return false, nil
end

local function waitForDepotEmpty(depot, timeoutSec)
  local deadline = os.clock() + timeoutSec

  while os.clock() < deadline do
    local ok, item = safeCall(depot.getItemDetail, 1)
    if ok and not item then
      return true
    end
    sleep(POLL)
  end

  return false
end

local function printHeader(title)
  print("")
  print(("="):rep(42))
  print(title)
  print(("="):rep(42))
end

-- ==================================================
-- INIT
-- ==================================================
local me = wrapPeripheral(ME_BRIDGE, "ME Bridge")
local packager = wrapPeripheral(PACKAGER, "Packager")
local bufferChest = wrapPeripheral(BUFFER_CHEST, "Buffer Chest")
local depot = wrapPeripheral(DEPOT, "Depot")

-- defaultně držíme SET low
redstone.setOutput(ARM_SET_REDSTONE_SIDE, false)

printHeader("MASTER POC")

local itemName = prompt("Item name", "minecraft:iron_ingot")
local count = tonumber(prompt("Count", "16"))
local address = prompt("Package address", "ORD0001|P01")

if not count or count < 1 then
  error("Neplatny count")
end

print("")
print("Item:         " .. tostring(itemName))
print("Count:        " .. tostring(count))
print("Address:      " .. tostring(address))
print("Buffer chest: " .. tostring(BUFFER_CHEST))
print("Depot:        " .. tostring(DEPOT))

-- ==================================================
-- 1) Nastav address na packageru
-- ==================================================
do
  local ok, res = safeCall(packager.setAddress, address)
  if not ok then
    error("packager.setAddress selhal: " .. tostring(res))
  end

  local ok2, currentAddress = safeCall(packager.getAddress)
  if not ok2 then
    error("packager.getAddress selhal: " .. tostring(currentAddress))
  end

  print("[1/7] Packager address:", tostring(currentAddress))

  if currentAddress ~= address then
    error("Packager address mismatch. Ocekavano " .. tostring(address) .. ", dostal jsem " .. tostring(currentAddress))
  end
end

-- ==================================================
-- 2) Export z ME do bridge chest
-- ==================================================
do
  local moved, err = me.exportItem(
    {
      name = itemName,
      count = count
    },
    BRIDGE_CHEST_DIRECTION
  )

  print("[2/7] exportItem moved:", tostring(moved), "err:", tostring(err))

  if not moved or moved < count then
    error("ME export nepresunul dost itemu. Requested=" .. tostring(count) .. " moved=" .. tostring(moved))
  end
end

-- ==================================================
-- 3) Počkej na item v buffer chest
-- ==================================================
do
  print("[3/7] Cekam na item v buffer chest...")

  local ok, actual = waitForBuffer(bufferChest, itemName, count, BUFFER_TIMEOUT)
  print("[3/7] Buffer result:", tostring(ok), "count:", tostring(actual))

  if not ok then
    error("Buffer chest nema dost itemu. Nasel jsem jen " .. tostring(actual))
  end
end

-- ==================================================
-- 4) Ruční potvrzení, že Create chain je ready
-- ==================================================
print("[4/7] Nech Create chain presunout item do packageru.")
print("       Az bude item ready k zabalení, stiskni ENTER.")
read()

-- ==================================================
-- 5) Vytvoř balík
-- ==================================================
do
  local ok, res = safeCall(packager.makePackage, true)
  print("[5/7] makePackage(true):", tostring(ok), tostring(res))

  if not ok then
    error("makePackage selhal: " .. tostring(res))
  end
end

-- ==================================================
-- 6) Přečti balík na inspection depotu
-- ==================================================
local depotPackageItem
do
  print("[6/7] Cekam na balicek na inspection depotu...")

  local ok, item = waitForDepotPackage(depot, DEPOT_TIMEOUT)
  if not ok then
    error("Balicek nedorazil na depot v casovem limitu")
  end

  depotPackageItem = item

  local pkg = item.package
  if not pkg then
    error("Depot item nema package API")
  end

  local okAddr, depotAddress = safeCall(function()
    return pkg:getAddress()
  end)

  if not okAddr then
    error("pkg:getAddress() selhal: " .. tostring(depotAddress))
  end

  print("[6/7] Depot address:", tostring(depotAddress))

  if depotAddress ~= address then
    error("Address mismatch na depotu. Ocekavano " .. tostring(address) .. ", prislo " .. tostring(depotAddress))
  end

  local okList, contents = safeCall(function()
    return pkg:list()
  end)

  if okList then
    print("Package contents:")
    dump(contents)
  else
    print("pkg:list() selhalo:", tostring(contents))
  end

  print("[6/7] Balicek overen.")
end

-- ==================================================
-- 7) Pusť arm přes SR latch SET pulse
-- ==================================================
do
  print("[7/7] Posilam SET pulse do SR latch...")
  pulseSet()

  print("[7/7] Cekam, az balicek zmizi z inspection depotu...")
  local cleared = waitForDepotEmpty(depot, DEPOT_CLEAR_TIMEOUT)

  if not cleared then
    print("WARN: Depot se nevyprázdnil v limitu. Mozna arm nedobehla nebo latch/reset nefunguje.")
  else
    print("[7/7] Balicek opustil inspection depot.")
  end
end

print("")
print("SUCCESS: Proof of concept probehl.")
