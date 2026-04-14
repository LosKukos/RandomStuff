-- poc_master.lua
-- Proof of concept:
-- ME -> bridge chest -> buffer chest -> packager -> depot -> dál

-- =========================
-- KONFIG
-- =========================
local ME_BRIDGE = "right"            -- side/jméno ME bridge periferie
local PACKAGER = "left"              -- side/jméno packager periferie
local DEPOT = "back"                 -- side/jméno depotu
local BRIDGE_CHEST_DIRECTION = "up"  -- směr od ME bridge do bridge chest
local CLUTCH_REDSTONE_SIDE = "top"   -- redstone výstup na clutch ZA depotem

-- clutch logika:
-- true  = redstone ON => belt jede
-- false = redstone OFF => belt jede
local CLUTCH_ON_HIGH = true

-- timeouty
local DEPOT_TIMEOUT = 10
local DEPOT_CLEAR_TIMEOUT = 5

-- polling
local POLL_INTERVAL = 0.1

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

local function clutchRun()
  clutchSet(true)
end

local function clutchStop()
  clutchSet(false)
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

local function dump(v)
  print(textutils.serialise(v))
end

local function safeCall(fn, ...)
  local ok, a, b, c, d = pcall(fn, ...)
  if not ok then
    return false, a, b, c, d
  end
  return true, a, b, c, d
end

local function wrapPeripheral(name, label)
  local p = peripheral.wrap(name)
  if not p then
    error("Nenalezena periferie pro " .. label .. ": " .. tostring(name))
  end
  return p
end

local function waitForDepotPackage(depot, timeoutSec)
  local deadline = os.clock() + timeoutSec

  while os.clock() < deadline do
    local ok, item = safeCall(depot.getItemDetail, 1)
    if ok and item and item.package then
      return true, item
    end
    sleep(POLL_INTERVAL)
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
    sleep(POLL_INTERVAL)
  end

  return false
end

local function getPackageAddress(pkg)
  local ok, addr = safeCall(function()
    return pkg:getAddress()
  end)

  if not ok then
    return false, addr
  end

  return true, addr
end

local function getPackageList(pkg)
  local ok, lst = safeCall(function()
    return pkg:list()
  end)

  if not ok then
    return false, lst
  end

  return true, lst
end

local function printHeader(title)
  print("")
  print(("="):rep(36))
  print(title)
  print(("="):rep(36))
end

-- =========================
-- INIT
-- =========================
local me = wrapPeripheral(ME_BRIDGE, "ME Bridge")
local packager = wrapPeripheral(PACKAGER, "Packager")
local depot = wrapPeripheral(DEPOT, "Depot")

-- defaultně drž dálkovou linku zavřenou
clutchStop()

printHeader("DEMO MASTER POC")

local itemName = prompt("Item name", "minecraft:iron_ingot")
local count = tonumber(prompt("Count", "32"))
local address = prompt("Package address", "ORD0001|P01")

if not count or count < 1 then
  error("Neplatný count")
end

print("")
print("Item:    " .. tostring(itemName))
print("Count:   " .. tostring(count))
print("Address: " .. tostring(address))
print("")

-- =========================
-- 1) Nastav address na packageru
-- =========================
do
  local okSet, setRes = safeCall(packager.setAddress, address)
  if not okSet then
    error("packager.setAddress selhal: " .. tostring(setRes))
  end

  local okGet, currentAddress = safeCall(packager.getAddress)
  if not okGet then
    error("packager.getAddress selhal: " .. tostring(currentAddress))
  end

  print("[1/6] Packager address:", tostring(currentAddress))

  if currentAddress ~= address then
    error("Address mismatch na packageru. Ocekavano " .. tostring(address) .. ", je " .. tostring(currentAddress))
  end
end

-- =========================
-- 2) Export z ME do bridge chest
-- =========================
do
  local moved, err = me.exportItem(
    {
      name = itemName,
      count = count
    },
    BRIDGE_CHEST_DIRECTION
  )

  print("[2/6] exportItem -> moved:", tostring(moved), "err:", tostring(err))

  if not moved or moved < count then
    error("ME export nedodal dost itemu. Requested=" .. tostring(count) .. " moved=" .. tostring(moved))
  end
end

-- =========================
-- 3) Počkej na Create chain
-- =========================
print("[3/6] Nech Create chain presunout item do packageru.")
print("       Az bude item ready k zabalení, stiskni ENTER.")
read()

-- =========================
-- 4) Vytvoř balík
-- =========================
do
  local okMake, makeRes = safeCall(packager.makePackage, true)
  print("[4/6] makePackage(true):", tostring(okMake), tostring(makeRes))

  if not okMake then
    error("makePackage selhal: " .. tostring(makeRes))
  end
end

-- =========================
-- 5) Počkej na balík na depotu
-- clutch za depotem je STOP, takže balík má zůstat na depotu
-- =========================
print("[5/6] Cekam na balicek na depotu...")

local gotPackage, depotItem = waitForDepotPackage(depot, DEPOT_TIMEOUT)
if not gotPackage then
  error("Balicek nedorazil na depot v časovém limitu")
end

print("[5/6] Balicek detekovan na depotu.")

-- =========================
-- 6) Přečti a ověř balík
-- =========================
do
  local pkg = depotItem.package
  if not pkg then
    error("Depot item nema package API")
  end

  local okAddr, depotAddress = getPackageAddress(pkg)
  if not okAddr then
    error("pkg:getAddress() selhal: " .. tostring(depotAddress))
  end

  print("[6/6] Depot address:", tostring(depotAddress))

  if depotAddress ~= address then
    error("Address mismatch na depotu. Ocekavano " .. tostring(address) .. ", prislo " .. tostring(depotAddress))
  end

  local okList, contents = getPackageList(pkg)
  if okList then
    print("")
    print("Package contents:")
    dump(contents)
  else
    print("pkg:list() selhalo:", tostring(contents))
  end

  print("")
  print("SUCCESS: Balicek byl uspesne vytvoren a overen.")
end

-- =========================
-- Uvolni balík dál
-- =========================
print("")
print("Stiskni ENTER pro pusteni balicku dal za depot.")
read()

clutchRun()
sleep(0.5)
clutchStop()

local cleared = waitForDepotEmpty(depot, DEPOT_CLEAR_TIMEOUT)
print("Depot empty:", tostring(cleared))
print("Hotovo.")
