# CC Package Node

Primitive checkpoint node for package tracking.

It does not decide routing. It only reports:

```json
{
  "packageId": "ORDxxxx|P01",
  "nodeId": "node_1",
  "nodeName": "Node 1",
  "event": "pass"
}
```

ESP is expected to assign real RTC/NTP time and store the route event.

## Files

- `config.lua` - node identity, scanner peripheral, ESP URL
- `util.lua` - helpers
- `esp.lua` - HTTP client
- `scanner.lua` - package address reading from depot/scanner peripheral
- `node.lua` - main loop

## Install location in Git

Recommended:

```text
Project ESP - CC/CC/node/
  config.lua
  util.lua
  esp.lua
  scanner.lua
  node.lua
```

On the ComputerCraft computer, keep all `.lua` files in the same folder and run:

```lua
node
```

or:

```lua
shell.run("node/node.lua")
```

## Required ESP endpoint

`POST /api/package/event`

Request:

```json
{
  "packageId": "ORDxxxx|P01",
  "nodeId": "node_1",
  "nodeName": "Node 1",
  "event": "pass"
}
```

ESP should generate and store NTP/RTC based time, not CC.
