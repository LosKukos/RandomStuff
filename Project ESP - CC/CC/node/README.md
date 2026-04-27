# CC Package Node

This is a dumb package checkpoint.

It does not decide destination or arrival. It only reports:

```txt
Package X passed through node Y.
```

## First start

Run:

```lua
node
```

The node registers itself with ESP using `nodeName`. ESP returns a stable `nodeId`, which is stored in `node_state.json`.

## Files

- `config.lua` - local node config
- `node.lua` - main loop
- `esp.lua` - HTTP API wrapper
- `scanner.lua` - package scanner
- `util.lua` - helpers
- `node_state.json` - generated locally, do not commit per-machine copies

## ESP API used

- `POST /api/node/register`
- `POST /api/node/heartbeat`
- `POST /api/package/event`
