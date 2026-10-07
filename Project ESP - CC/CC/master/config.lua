local CFG = {
  -- peripherals
  meBridge = "me_bridge_0",
  packager = "Create_Packager_0",
  bufferChest = "minecraft:barrel_0",
  depot = "create:depot_2",
  bridgeChestDirection = "right",
  armSetRedstoneSide = "back",
  playerDetector = "player_detector_0",   -- Advanced Peripherals Player Detector (nazev periferie)

  -- esp
  espBase = "http://10.0.1.17",

  -- timing
  bufferAppearTimeout = 10,
  depotTimeout = 10,
  depotClearTimeout = 5,
  settleDelay = 0.5,
  setPulse = 0.15,
  poll = 0.1,
  idleSleep = 3,

  -- ME snapshot pro web (katalog polozek)
  meSnapshotInterval = 30,   -- s
  meSnapshotMax = 300,       -- max poloznek (nejpocetnejsi prvni), kvuli RAM na ESP
}

return CFG
