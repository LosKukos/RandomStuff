-- demo_master.lua
-- Jednoduchý end-to-end test:
-- ME -> bridge chest -> buffer chest -> packager -> depot

-- =========================
-- CONFIG
-- =========================
local ME_BRIDGE = "right"                 -- peripheral name nebo side
local PACKAGER = "left"                   -- peripheral name nebo side
local BUFFER_INV = "minecraft:chest_0"    -- jméno buffer chestky
local DEPOT = "back"                      -- peripheral name nebo side depotu
local CLUTCH_REDSTONE_SIDE = "top"        -- redstone side pro clutch

-- jak dlouho čekat na jednotlivé kroky
local EXPORT_TIMEOUT = 5
local PACKAGE_TIMEOUT = 8

-- clutch režim:
-- true = clutch zapnutá když je redstone ON
-- false = clutch zapnutá když je redstone OFF
local CLUTCH_ON_HIGH = true

-- =========================
-- HELPERS
-- =========================
local function clutchSet(enabled)
  local signal = enabled
  if not CLUTCH_ON_HIGH then
    signal = not enabled
  end
  redstone.setOutput(CLUTCH_REDSTONE_SIDE, signal)
end

local function clutchStop()
  clutchSet(false)
end

local function clutchRun()
  clutchSet(true)
end

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

local function dump(value)
  print(textutils.serialise(value))
end

local function now()
  return os.epoch("utc")
end

local function wrapPeripheral(name, label)
  local p = peripheral.wrap(name)
  if not p then
    error("Nenalezl jsem periferii pro " .. label .. ": " .. tostring(name))
  end
  return p
end

local function safeCall(fn, ...)
  local ok, a, b, c, d = pcall(fn, ...)
  if not ok then
    return false, a
  end
  return true, a, b, c, d
end

local function getBufferItemCount(inv, wantedName)
  local ok, list = safeCall(inv.list)
  if not ok or type(list) ~= "table" then
    return 0
  end

  local total = 0
  for _, item in pairs(list) do
    if item.name == wantedName then
      total = total + (item.count or 0)
    end
  end
  return total
end

local function waitForBufferCount(inv, wantedName, minCount, timeoutSec)
  local deadline = os.clock() + timeoutSec

  while os.clock() < deadline do
    local count = getBufferItemCount(inv, wantedName)
    if count >= minCount then
      return true, count
    end
    sleep(0.1)
  end

  return false, getBufferItemCount(inv, wantedName)
end

local function waitForDepotPackage(depot, timeoutSec)
  local deadline = os.clock() + timeoutSec

  while os.clock() < deadline do
    local ok, item = safeCall(depot.getItemDetail, 1)
    if ok and item and item.package then
      return true, item
    end
    sleep(0.1)
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
    sleep(0.1)
  end

  return false
end

-- =========================
-- INIT
-- =========================
local me = wrapPeripheral(ME_BRIDGE, "ME bridge")
local packager = wrapPeripheral(PACKAGER, "Packager")
local bufferInv = wrapPeripheral(BUFFER_INV, "Buffer chest")
local depot = wrapPeripheral(DEPOT, "Depot")

-- defaultně zastavit pohyb za packagerem/depotem
clutchStop()

print("=== DEMO MASTER TEST ===")
print("Time:", now())

local itemName = prompt("Item name", "minecraft:iron_ingot")
local count = tonumber(prompt("Count", "16"))
local address = prompt("Package address", "TEST|P01")

if not count or count < 1 then
  error("Neplatny count")
end

print("")
print("Item:", itemName)
print("Count:", count)
print("Address:", address)
print("Buffer inventory:", BUFFER_INV)
print("")

-- =========================
-- 1) nastav address packageru
-- =========================
do
  local ok, res = safeCall(packager.setAddress, address)
  if not ok then
    error("packager.setAddress selhal: " .. tostring(res))
  end

  local ok2, currentAddress = safeCall(packager.getAddress)
  if not ok2 then
    error("packager.getAddress selhal: " .. tostring(currentAddress))
  end

  print("[1/6] Packager address set:", currentAddress)

  if currentAddress ~= address then
    error("Packager address mismatch. Ocekavano " .. tostring(address) .. ", dostal jsem " .. tostring(currentAddress))
  end
end

-- =========================
-- 2) export z ME do buffer chest
-- =========================
do
  local moved, err = me.exportItemToPeripheral(
    {
      name = itemName,
      count = count
    },
    BUFFER_INV
  )

  print("[2/6] exportItemToPeripheral moved:", tostring(moved), "err:", tostring(err))

  if not moved or moved < count then
    error("ME export nepresunul dost itemu. Requested=" .. tostring(count) .. " moved=" .. tostring(moved))
  end
end

-- =========================
-- 3) zkontroluj buffer chest
-- =========================
do
  local ok, actual = waitForBufferCount(bufferInv, itemName, count, EXPORT_TIMEOUT)
  print("[3/6] Buffer check:", ok, "count:", actual)

  if not ok then
    error("Buffer chest nedostala dost itemu. Ma jen " .. tostring(actual))
  end
end

print("[4/6] Stiskni ENTER az Create chain presune item do packageru a jsi ready na makePackage()")
read()

-- =========================
-- 4) make package
-- =========================
do
  local ok, res = safeCall(packager.makePackage, true)
  print("[4/6] makePackage:", ok, tostring(res))

  if not ok then
    error("makePackage selhal: " .. tostring(res))
  end
end

-- =========================
-- 5) pust balicek na depot
-- =========================
print("[5/6] Poustim clutch, cekam na balicek na depotu...")
clutchRun()

local gotPackage, depotItem = waitForDepotPackage(depot, PACKAGE_TIMEOUT)

-- hned znovu stop, at ti to neujede do Narnie
clutch
