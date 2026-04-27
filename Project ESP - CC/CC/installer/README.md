# CC Installer

ComputerCraft installer for Project ESP - CC.

## Git location

Put this folder here:

```txt
Project ESP - CC/CC/installer/
```

## Files

```txt
install.lua
README.md
```

## Usage on a fresh CC computer

Download `install.lua` to the CC computer and run:

```lua
install
```

The installer will ask whether to install:

- `master` - factory/order fulfillment computer
- `node` - dumb package checkpoint sensor

It downloads files from:

```txt
https://raw.githubusercontent.com/LosKukos/RandomStuff/main/Project%20ESP%20-%20CC/CC
```

Then it patches the local `config.lua` based on your answers and creates a runner:

```lua
master
```

or

```lua
node
```

## Requirements

ComputerCraft HTTP API must be enabled.
