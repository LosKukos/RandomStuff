-- loader_gate.lua
-- AXIS Train Loader Gate
-- Claims next packed order from ESP, loads exact packages, releases train only after ESP confirms load-complete.
--
-- Physical expected layout:
--   package loop -> scan depot -> CC decision
--       accept pulse -> one package to train loader line
--       reject pulse -> one package back to loop
--
-- Train:
--   standing at Factory/main station
--   schedule is set only after all packages are confirmed loaded
--   train release is pulsed only after /api/orders/load-complete succeeds

local CFG = {
  espBase = "http://10.0.1.17",

  -- Package scan holding point
  scanDepot = "create:depot_0",

  -- Optional confirm scan point after accept path.
  -- If nil/empty, loader treats "scan depot empty after accept" as loaded.
  -- Recommended later: place a depot/reader after accept sorter and set this.
  confirmDepot = nil,

  -- Create train station peripheral
  trainStation = "create:station_0",

  -- Train schedule
  homeStation = "Factory",

  -- Redstone outputs
  acceptSide = "left",       -- pulse sorter/filter path to train loader
  rejectSide = "right",      -- pulse sorter/filter path back to package loop
  trainReleaseSide = "back", -- pulse Create powered condition at Factory

  -- Timing
  movePulse = 0.20,
  trainPulse = 0.50,
  poll = 0.10,
  moveTimeout = 5,
  confirmTimeout = 8,
  loadTimeout = 240,
  idleSleep = 5,

  -- If true, after one finished load it asks ESP for another order.
  loopForever = true,
}

-- =========================
-- UTILS
-- =========================

local function dump(v)
  local function sanitize(value, seen)
    if type(value) == "table" then
      if seen[value] then return "<repeated>" end
      seen[value] = true

      local out = {}
      for k, item in pairs(value) do
        out[tostring(k)] = sanitize(item, seen)
      end
      return out
    end

    if type(value) == "function" then return "<function>" end
    if type(value) == "userdata" or type(value) == "thread" then return tostring(value) end
    return value
  end

  local ok, res = pcall(textutils.serialise, v)
  if ok then print(res) return end

  local ok2, res2 = pcall(textutils.serialise, sanitize(v, {}))
  if ok2 then print(res2) else print(tostring(v)) end
end

local function header(title)
  print("")
  print(("="):rep(52))
  print(title)
  print(("="):rep(52))
end

local function pulse(side, seconds)
  if not side or side == "" then return end
  redstone.setOutput(side, true)
  sleep(seconds)
  redstone.setOutput(side, false)
end

local function countMap(t)
  local n = 0
  for _ in pairs(t) do n = n + 1 end
  return n
end

local function safePeripheral(name, label)
  if not name or name == "" then return nil end
  local p = peripheral.wrap(name)
  if not p then
    error(label .. " not found: " .. tostring(name))
  end
  return p
end

local function postJson(path, payload)
  local url = CFG.espBase .. path
  local body = textutils.serialiseJSON(payload or {})

  local res, err = http.post(url, body, {
    ["Content-Type"] = "application/json"
  })

  if not res then
    return false, {
      reason = "http_post_failed",
      error = err,
      url = url
    }
  end

  local raw = res.readAll()
  res.close()

  local ok, data = pcall(textutils.unserialiseJSON, raw)
  if not ok or type(data) ~= "table" then
    return false, {
      reason = "invalid_json_response",
      raw = raw,
      url = url
    }
  end

  if data.ok == false then
    return false, data
  end

  return true, data
end

-- =========================
-- ESP API
-- =========================

local function claimNextLoad()
  return postJson("/api/orders/claim-next-load", {})
end

local function markPackageLoaded(orderId, packageId)
  return postJson("/api/package/loaded", {
    orderId = orderId,
    packageId = packageId,
    loaderName = "Factory Loader Gate"
  })
end

local function markLoadComplete(orderId)
  return postJson("/api/orders/load-complete", {
    orderId = orderId
  })
end

local function updateOrder(orderId, status, meta)
  return postJson("/api/orders/update", {
    orderId = orderId,
    status = status,
    meta = meta or {}
  })
end

-- =========================
-- PACKAGE SCAN
-- =========================

local scanDepot = safePeripheral(CFG.scanDepot, "Scan depot")
local confirmDepot = nil
if CFG.confirmDepot and CFG.confirmDepot ~= "" then
  confirmDepot = safePeripheral(CFG.confirmDepot, "Confirm depot")
end

local function getItemFrom(inv)
  local ok, item = pcall(function()
    return inv.getItemDetail(1)
  end)

  if ok and item then return item end

  -- fallback for inventories where list() works better
  local okList, items = pcall(function()
    return inv.list()
  end)

  if okList and type(items) == "table" then
    for _, listed in pairs(items) do
      if listed then return listed end
    end
  end

  return nil
end

local function readPackageIdFrom(inv)
  local item = getItemFrom(inv)
  if not item or not item.package then
    return nil
  end

  local ok, addr = pcall(function()
    return item.package:getAddress()
  end)

  if not ok or not addr or addr == "" then
    return nil
  end

  return addr
end

local function readPackageId()
  return readPackageIdFrom(scanDepot)
end

local function waitDepotEmpty(inv, timeout)
  local deadline = os.clock() + timeout

  while os.clock() < deadline do
    local item = getItemFrom(inv)
    if not item then return true end
    sleep(CFG.poll)
  end

  return false
end

local function waitConfirmLoaded(packageId, timeout)
  if not confirmDepot then
    -- Fallback mode: accept path is considered loaded once scan depot is empty.
    return true
  end

  local deadline = os.clock() + timeout

  while os.clock() < deadline do
    local seenId = readPackageIdFrom(confirmDepot)
    if seenId == packageId then
      return true
    end
    sleep(CFG.poll)
  end

  return false
end

-- =========================
-- TRAIN SCHEDULE
-- =========================

local function poweredCondition()
  return {
    {
      {
        id = "create:powered",
        data = {}
      }
    }
  }
end

local function cargoEmptyCondition()
  return {
    {
      {
        id = "create:item_threshold",
        data = {
          threshold = "0",
          item = {},
          measure = 0,
          operator = 2
        }
      }
    }
  }
end

local function destinationEntry(stationName, conditions)
  return {
    instruction = {
      id = "create:destination",
      data = {
        text = stationName
      }
    },
    conditions = conditions
  }
end

local function makeDeliverySchedule(targetStation, homeStation)
  return {
    cyclic = false,
    entries = {
      destinationEntry(targetStation, cargoEmptyCondition()),
      destinationEntry(homeStation, poweredCondition())
    }
  }
end

local function setupTrainSchedule(targetStation)
  if not CFG.trainStation or CFG.trainStation == "" then
    print("[TRAIN] schedule disabled")
    return true
  end

  local station = peripheral.wrap(CFG.trainStation)
  if not station then
    return false, "train_station_not_found"
  end

  if not station.isTrainPresent() then
    return false, "no_train_present"
  end

  local okTarget, reasonTarget = station.canTrainReach(targetStation)
  if not okTarget then
    return false, "cannot_reach_target: " .. tostring(reasonTarget)
  end

  local okHome, reasonHome = station.canTrainReach(CFG.homeStation)
  if not okHome then
    return false, "cannot_reach_home: " .. tostring(reasonHome)
  end

  local schedule = makeDeliverySchedule(targetStation, CFG.homeStation)
  station.setSchedule(schedule)

  print("[TRAIN] schedule set: " .. tostring(targetStation) .. " -> " .. tostring(CFG.homeStation))
  return true
end

-- =========================
-- LOADER STATE
-- =========================

local expected = {}
local loaded = {}

local function allLoaded()
  for packageId in pairs(expected) do
    if not loaded[packageId] then
      return false
    end
  end
  return true
end

local function printProgress()
  print("[LOAD] " .. countMap(loaded) .. "/" .. countMap(expected) .. " loaded")
end

local function printMissing()
  print("[LOAD] Missing packages:")
  for packageId in pairs(expected) do
    if not loaded[packageId] then
      print(" - " .. packageId)
    end
  end
end

local function acceptPackage(orderId, packageId)
  print("[ACCEPT] " .. packageId)

  pulse(CFG.acceptSide, CFG.movePulse)

  if not waitDepotEmpty(scanDepot, CFG.moveTimeout) then
    print("[WARN] scan depot not empty after accept")
    return false
  end

  if not waitConfirmLoaded(packageId, CFG.confirmTimeout) then
    print("[WARN] package not confirmed after accept: " .. packageId)
    return false
  end

  local ok, res = markPackageLoaded(orderId, packageId)
  if not ok then
    print("[ESP] package loaded failed")
    dump(res)
    return false
  end

  loaded[packageId] = true
  printProgress()

  return true
end

local function rejectPackage(packageId)
  print("[REJECT] " .. tostring(packageId))

  pulse(CFG.rejectSide, CFG.movePulse)

  if not waitDepotEmpty(scanDepot, CFG.moveTimeout) then
    print("[WARN] scan depot not empty after reject")
    return false
  end

  return true
end

local function handlePackage(orderId, packageId)
  if expected[packageId] and not loaded[packageId] then
    return acceptPackage(orderId, packageId)
  end

  return rejectPackage(packageId)
end

local function buildExpected(packages)
  expected = {}
  loaded = {}

  for _, pkg in ipairs(packages or {}) do
    local packageId = pkg.packageId or pkg.address
    local status = tostring(pkg.status or "")

    if packageId and packageId ~= "" then
      -- claim-next-load should return packed packages, but keep this tolerant.
      if status ~= "loaded" and status ~= "delivered" and status ~= "failed" then
        expected[packageId] = true
      end
    end
  end
end

local function runLoadLoop(order)
  local orderId = order.orderId
  local deadline = os.clock() + CFG.loadTimeout

  while os.clock() < deadline do
    if allLoaded() then
      print("[LOAD] local complete, asking ESP to verify")

      local okComplete, completeRes = markLoadComplete(orderId)
      if not okComplete then
        print("[ESP] load-complete rejected")
        dump(completeRes)
        return false, "esp_load_complete_failed"
      end

      print("[LOAD] ESP confirmed load-complete")
      return true
    end

    local packageId = readPackageId()
    if packageId then
      handlePackage(orderId, packageId)
    end

    sleep(CFG.poll)
  end

  print("[LOAD] timeout")
  printMissing()

  updateOrder(orderId, "load_timeout", {
    loaded = loaded
  })

  return false, "load_timeout"
end

local function releaseTrainAfterLoad(order)
  local target = order.destination

  if not target or target == "" then
    return false, "missing_destination"
  end

  local okSchedule, scheduleErr = setupTrainSchedule(target)
  if not okSchedule then
    return false, scheduleErr
  end

  sleep(1)

  print("[TRAIN] releasing after confirmed load")
  pulse(CFG.trainReleaseSide, CFG.trainPulse)

  return true
end

-- =========================
-- MAIN CYCLE
-- =========================

local function loadOneClaimedOrder(order, packages)
  header("ORDER CLAIMED")
  print("Order: " .. tostring(order.orderId))
  print("Status: " .. tostring(order.status))
  print("Destination: " .. tostring(order.destination))
  print("Packages: " .. tostring(#packages))

  buildExpected(packages)

  if countMap(expected) == 0 then
    print("[LOAD] nothing to load")
    return false
  end

  header("EXPECTED PACKAGES")
  for packageId in pairs(expected) do
    print(" - " .. packageId)
  end

  header("LOADING")
  local okLoad, loadErr = runLoadLoop(order)
  if not okLoad then
    header("FAILED")
    print("Loading failed: " .. tostring(loadErr))
    return false
  end

  header("TRAIN RELEASE")
  local okRelease, releaseErr = releaseTrainAfterLoad(order)
  if not okRelease then
    print("[TRAIN] release failed: " .. tostring(releaseErr))
    updateOrder(order.orderId, "load_failed", {
      reason = "train_release_failed",
      detail = tostring(releaseErr)
    })
    return false
  end

  header("DONE")
  print("Order loaded and train released.")
  return true
end

local function cycle()
  header("CLAIM NEXT LOAD")

  local okClaim, claimRes = claimNextLoad()
  if not okClaim then
    print("[ESP] claim-next-load failed")
    dump(claimRes)
    sleep(CFG.idleSleep)
    return
  end

  local data = claimRes.data or {}
  local order = data.order
  local packages = data.packages or {}

  if not order then
    print("[ESP] no packed order ready for loading")
    sleep(CFG.idleSleep)
    return
  end

  loadOneClaimedOrder(order, packages)
end

local function main()
  header("AXIS TRAIN LOADER GATE")

  print("ESP: " .. tostring(CFG.espBase))
  print("Scan depot: " .. tostring(CFG.scanDepot))
  print("Confirm depot: " .. tostring(CFG.confirmDepot or "disabled"))
  print("Train station: " .. tostring(CFG.trainStation))
  print("Home station: " .. tostring(CFG.homeStation))
  print("Loop forever: " .. tostring(CFG.loopForever))

  repeat
    local ok, err = pcall(cycle)
    if not ok then
      print("")
      print("[CYCLE CRASH]")
      print(err)
      sleep(CFG.idleSleep)
    end
  until not CFG.loopForever
end

main()
