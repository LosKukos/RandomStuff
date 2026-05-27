-- players.lua
-- Syncs online player list to ESP.
-- Called from master main loop periodically.
-- Requires: CC API (players()) or OpenPeripherals/AdvPeripherals sensor.

local CFG = require("config")
local M = {}

local lastPlayersJson = nil

local function getPlayers()
  -- Try native CC players() first (works on some server setups)
  if type(players) == "function" then
    local ok, list = pcall(players)
    if ok and type(list) == "table" then return list end
  end

  -- Fallback: try peripheral (AdvPeripherals playerDetector or similar)
  -- Adjust peripheral name to match your setup
  local detector = peripheral.find("playerDetector")
  if detector and detector.getOnlinePlayers then
    local ok, list = pcall(detector.getOnlinePlayers)
    if ok and type(list) == "table" then return list end
  end

  -- Nothing available yet
  return nil
end

function M.sync()
  local list = getPlayers()

  if not list then
    return false, "no_player_source"
  end

  local json = textutils.serialiseJSON(list)

  if json == lastPlayersJson then
    return true, "no_change"
  end

  local url = CFG.espBase .. "/api/players/online"
  local res, err = http.post(url, json, { ["Content-Type"] = "application/json" })

  if not res then
    return false, "http_failed: " .. tostring(err)
  end

  res.close()
  lastPlayersJson = json
  return true, "synced"
end

return M
