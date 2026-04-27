local CFG = {
  -- Unique checkpoint identity.
  -- Keep this stable, because ESP will use it for package route history.
  nodeId = "node_1",
  nodeName = "Node 1",

  -- Peripheral holding/scanning the package briefly.
  -- Example: "create:depot_0" or side name like "front".
  scanner = "create:depot_0",

  -- Optional redstone release.
  -- If your belt/pipe moves packages without CC help, set releaseEnabled = false.
  releaseEnabled = false,
  releaseSide = "back",
  releasePulse = 0.15,

  -- ESP backend.
  espBase = "http://10.0.1.17",

  -- Node behavior.
  event = "pass",
  poll = 0.10,
  debounceSeconds = 2.0,

  -- If true, prints successful repeated scan ignores.
  verboseDebounce = false,
}

return CFG
