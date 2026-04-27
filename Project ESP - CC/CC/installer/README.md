# Project ESP - CC Installer

Installs either the CC Master or the CC Node package from GitHub.

## Install location

The installer writes files into:

```txt
axis/master/
axis/node/
```

and creates root runners:

```txt
master
node
```

Run with:

```lua
master
```

or:

```lua
node
```

## Node identity

Node identity is assigned by ESP on first startup.

The node stores its assigned ID locally in:

```txt
axis/node/node_state.json
```

Do **not** commit `node_state.json` to Git. That file is runtime state for one specific CC computer.

## Usage

Download `install.lua` to a ComputerCraft computer and run:

```lua
install
```

HTTP must be enabled in ComputerCraft.
