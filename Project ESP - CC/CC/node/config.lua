local CFG = {
  nodeName = "Node 1",
  stateFile = "node_state.json",
  scanner = "create:depot_0",
  releaseEnabled = false,
  releaseSide = "back",
  releasePulse = 0.15,
  espBase = "http://10.0.1.17",
  event = "pass",
  poll = 0.10,
  debounceSeconds = 2.0,
  heartbeatSeconds = 60,
  verboseDebounce = false,
}

return CFG
