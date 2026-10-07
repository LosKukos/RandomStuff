-- players.lua
-- Syncs online player list to ESP.
-- Called from master main loop periodically.
-- Requires: CC API (players()) or OpenPeripherals/AdvPeripherals sensor.

local CFG = require("config")
local M = {}

local lastPlayersJson = nil
local lastSentAt = nil
-- ESP drzi seznam hracu jen v RAM. Kdyby se seznam nemenil, po restartu ESP by zustal
-- prazdny, proto ho posilame znovu aspon jednou za RESEND_EVERY sekund.
local RESEND_EVERY = 300

local detector = nil

local function getDetector()
  if detector then return detector end
  if CFG.playerDetector and CFG.playerDetector ~= "" then
    detector = peripheral.wrap(CFG.playerDetector)
  end
  if not detector then detector = peripheral.find("playerDetector") end
  return detector
end

local function getPlayers()
  -- Try native CC players() first (works on some server setups)
  if type(players) == "function" then
    local ok, list = pcall(players)
    if ok and type(list) == "table" then return list end
  end

  -- Advanced Peripherals Player Detector: nazev z config.lua (CFG.playerDetector),
  -- kdyz neexistuje, zkusime najit jakykoli "playerDetector" v siti.
  local det = getDetector()
  if det and det.getOnlinePlayers then
    local ok, list = pcall(det.getOnlinePlayers)
    if ok and type(list) == "table" then return list end
    detector = nil  -- odpojeny/rozbity - priste znovu najit
  end

  -- Nothing available yet
  return nil
end

function M.sync()
  local list = getPlayers()

  if not list then
    return false, "no_player_detector (CFG.playerDetector=" .. tostring(CFG.playerDetector) .. ")"
  end

  local json = textutils.serialiseJSON(list)
  local now = os.clock()

  if json == lastPlayersJson and lastSentAt and (now - lastSentAt) < RESEND_EVERY then
    return true, "no_change"
  end

  local url = CFG.espBase .. "/api/players/online"
  local res, err = http.post(url, json, { ["Content-Type"] = "application/json" })

  if not res then
    return false, "http_failed: " .. tostring(err)
  end

  res.close()
  lastPlayersJson = json
  lastSentAt = now
  return true, "synced"
end

return M
