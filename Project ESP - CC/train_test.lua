local CFG = {
  station = "create:station_0",
  target = "Kuba",
  home = "Factory",
  cyclic = false
}

local st = peripheral.wrap(CFG.station)
if not st then
  error("Station not found: " .. CFG.station)
end

local function dump(v)
  print(textutils.serialise(v))
end

local function destinationEntry(stationName, conditionId)
  conditionId = conditionId or "create:powered"

  return {
    instruction = {
      id = "create:destination",
      data = {
        text = stationName
      }
    },
    conditions = {
      {
        {
          id = conditionId,
          data = {}
        }
      }
    }
  }
end

local function makeSchedule(targetStation, homeStation)
  return {
    cyclic = CFG.cyclic,
    entries = {
      destinationEntry(targetStation, "create:powered"),
      destinationEntry(homeStation, "create:powered")
    }
  }
end

print("Station:", st.getStationName())
print("Train:", st.getTrainName())
print("Present:", st.isTrainPresent())
print("Has schedule:", st.hasSchedule())

if not st.isTrainPresent() then
  error("No train present")
end

local ok, reason = st.canTrainReach(CFG.target)
print("Can reach target:", ok, reason)

if not ok then
  error("Cannot reach target: " .. tostring(reason))
end

local okHome, reasonHome = st.canTrainReach(CFG.home)
print("Can reach home:", okHome, reasonHome)

if not okHome then
  error("Cannot reach home: " .. tostring(reasonHome))
end

local schedule = makeSchedule(CFG.target, CFG.home)

print("New schedule:")
dump(schedule)

st.setSchedule(schedule)

print("Schedule set.")
