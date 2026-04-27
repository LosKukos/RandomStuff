local CFG = {
  -- Human-readable checkpoint name.
  -- ESP will assign a stable nodeId automatically on first registration.
  nodeName = "Node 1",

  -- Local file used to remember the ESP-assigned nodeId.
  stateFile = "node_state.json",

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
  heartbeatSeconds = 60,

  -- If true, prints successful repeated scan ignores.
  verboseDebounce = false,
}

return CFG
