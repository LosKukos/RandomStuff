# CC Package Node

Dumb checkpoint - reports package passage to ESP.

## First start

Run: `node`

ESP assigns nodeId on first registration, stored in `node_state.json`.

## ESP API used

- POST /api/node/register
- POST /api/node/heartbeat
- POST /api/package/event
