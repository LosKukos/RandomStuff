-- installer/install.lua
-- CC installer for Project ESP - CC
-- Installs either master or node package from GitHub into this ComputerCraft computer.
--
-- v1.1.0
-- - Node ID is no longer configured manually.
-- - ESP assigns nodeId on first registration.
-- - node_state.json stays local on the CC computer.
-- - Installs code into axis/master or axis/node and creates root runners: master / node.

local INSTALLER_VERSION = "1.1.0"

local BASE_RAW = "https://raw.githubusercontent.com/LosKukos/RandomStuff/main/Project%20ESP%20-%20CC/CC"
local INSTALL_ROOT = "axis"

local PACKAGES = {
  master = {
    label = "Master - factory order fulfillment",
    sourceDir = "master",
    targetDir = fs.combine(INSTALL_ROOT, "master"),
    runnerName = "master",
    entry = "master.lua",
    files = {
      "config.lua",
      "util.lua",
      "esp.lua",
      "me.lua",
      "factory.lua",
      "orders.lua",
      "master.lua"
    }
  },
  node = {
    label = "Node - dumb package checkpoint",
    sourceDir = "node",
    targetDir = fs.combine(INSTALL_ROOT, "node"),
    runnerName = "node",
    entry = "node.lua",
    files = {
      "config.lua",
      "util.lua",
      "esp.lua",
      "scanner.lua",
      "node.lua",
      "README.md"
    }
  }
}

local function header(title)
  print("")
  print(("="):rep(48))
  print(title)
  print(("="):rep(48))
end

local function trim(s)
  return tostring(s or ""):match("^%s*(.-)%s*$")
end

local function ask(prompt, default)
  write(prompt)
  if default ~= nil and default ~= "" then
    write(" [" .. tostring(default) .. "]")
  end
  write(": ")

  local value = trim(read())
  if value == "" and default ~= nil then
    return default
  end
  return value
end

local function askBool(prompt, default)
  local suffix = default and "Y/n" or "y/N"
  while true do
    write(prompt .. " (" .. suffix .. "): ")
    local v = trim(read()):lower()

    if v == "" then return default end
    if v == "y" or v == "yes" or v == "j" or v == "jo" then return true end
    if v == "n" or v == "no" or v == "ne" then return false end

    print("Please answer y/n. Ano, lidská řeč je těžká.")
  end
end

local function ensureHttp()
  if not http or not http.get then
    error("HTTP API is disabled. Enable HTTP in ComputerCraft config first.")
  end
end

local function urlEncodePathPart(s)
  return tostring(s):gsub(" ", "%%20")
end

local function buildUrl(pkg, fileName)
  return BASE_RAW .. "/" .. urlEncodePathPart(pkg.sourceDir) .. "/" .. urlEncodePathPart(fileName)
end

local function download(url)
  local res, err = http.get(url)
  if not res then
    return nil, err or "http_get_failed"
  end

  local body = res.readAll()
  res.close()

  if not body or body == "" then
    return nil, "empty_response"
  end

  return body
end

local function ensureDir(path)
  if fs.exists(path) and not fs.isDir(path) then
    error("Target exists but is not a directory: " .. path)
  end

  if fs.exists(path) then return end

  local parts = {}
  for part in string.gmatch(path, "[^/]+") do
    table.insert(parts, part)
  end

  local current = ""
  for _, part in ipairs(parts) do
    current = current == "" and part or fs.combine(current, part)
    if fs.exists(current) and not fs.isDir(current) then
      error("Path component exists but is not a directory: " .. current)
    end
    if not fs.exists(current) then
      fs.makeDir(current)
    end
  end
end

local function writeFile(path, content)
  local dir = fs.getDir(path)
  if dir and dir ~= "" then
    ensureDir(dir)
  end

  local f = fs.open(path, "w")
  if not f then
    error("Could not write file: " .. path)
  end

  f.write(content)
  f.close()
end

local function readFile(path)
  local f = fs.open(path, "r")
  if not f then return nil end

  local data = f.readAll()
  f.close()

  return data
end

local function patchStringField(content, key, value)
  local replacement = key .. " = " .. string.format("%q", tostring(value)) .. ","
  local updated, count = content:gsub(key .. "%s*=%s*\"[^\"]*\"%s*,", replacement)

  if count == 0 then
    updated, count = content:gsub(key .. "%s*=%s*'[^']*'%s*,", replacement)
  end

  return updated
end

local function patchNumberField(content, key, value)
  local replacement = key .. " = " .. tostring(value) .. ","
  return (content:gsub(key .. "%s*=%s*[%d%.]+%s*,", replacement))
end

local function patchBoolField(content, key, value)
  local replacement = key .. " = " .. tostring(value) .. ","
  return (content:gsub(key .. "%s*=%s*[%a]+%s*,", replacement))
end

local function downloadPackage(pkg)
  ensureDir(pkg.targetDir)

  for _, fileName in ipairs(pkg.files) do
    local url = buildUrl(pkg, fileName)
    local target = fs.combine(pkg.targetDir, fileName)

    print("[GET] " .. pkg.sourceDir .. "/" .. fileName)
    local body, err = download(url)
    if not body then
      error("Failed to download " .. fileName .. ": " .. tostring(err))
    end

    writeFile(target, body)
  end
end

local function patchConfigMaster(path, values)
  local content = readFile(path)
  if not content then error("Missing config: " .. path) end

  local replacements = {
    meBridge = values.meBridge,
    packager = values.packager,
    bufferChest = values.bufferChest,
    depot = values.depot,
    bridgeChestDirection = values.bridgeChestDirection,
    armSetRedstoneSide = values.armSetRedstoneSide,
    espBase = values.espBase,
  }

  for key, value in pairs(replacements) do
    content = patchStringField(content, key, value)
  end

  writeFile(path, content)
end

local function patchConfigNode(path, values)
  local content = readFile(path)
  if not content then error("Missing config: " .. path) end

  local stringReplacements = {
    nodeName = values.nodeName,
    stateFile = values.stateFile,
    scanner = values.scanner,
    releaseSide = values.releaseSide,
    espBase = values.espBase,
    event = values.event,
  }

  for key, value in pairs(stringReplacements) do
    content = patchStringField(content, key, value)
  end

  content = patchBoolField(content, "releaseEnabled", values.releaseEnabled)
  content = patchNumberField(content, "releasePulse", values.releasePulse)
  content = patchNumberField(content, "poll", values.poll)
  content = patchNumberField(content, "debounceSeconds", values.debounceSeconds)
  content = patchNumberField(content, "heartbeatSeconds", values.heartbeatSeconds)
  content = patchBoolField(content, "verboseDebounce", values.verboseDebounce)

  writeFile(path, content)
end

local function createRunner(pkg)
  local runnerName = pkg.runnerName

  if fs.exists(runnerName) and fs.isDir(runnerName) then
    error("Cannot create runner '" .. runnerName .. "' because a directory with that name exists. Remove/rename it first.")
  end

  local targetDir = pkg.targetDir
  local entry = pkg.entry

  local script = table.concat({
    "local oldDir = shell.dir()",
    "shell.setDir(" .. string.format("%q", targetDir) .. ")",
    "local ok, err = pcall(function() shell.run(" .. string.format("%q", entry) .. ") end)",
    "shell.setDir(oldDir)",
    "if not ok then error(err, 0) end",
  }, "\n") .. "\n"

  writeFile(runnerName, script)
  print("[OK] Created runner: " .. runnerName)
end

local function configureMaster(pkg)
  header("Master config")

  local values = {
    espBase = ask("ESP base URL", "http://10.0.1.17"),
    meBridge = ask("ME bridge peripheral", "me_bridge_0"),
    packager = ask("Create packager peripheral", "Create_Packager_0"),
    bufferChest = ask("Buffer chest/barrel peripheral", "minecraft:barrel_0"),
    depot = ask("Inspection depot peripheral", "create:depot_2"),
    bridgeChestDirection = ask("ME export direction", "right"),
    armSetRedstoneSide = ask("Release redstone side", "back"),
  }

  patchConfigMaster(fs.combine(pkg.targetDir, "config.lua"), values)
end

local function configureNode(pkg)
  header("Node config")

  local releaseEnabled = askBool("Should node pulse redstone release after reporting?", false)

  local values = {
    espBase = ask("ESP base URL", "http://10.0.1.17"),
    nodeName = ask("Node display name", "Node 1"),
    stateFile = ask("Node state file", "node_state.json"),
    scanner = ask("Scanner/depot peripheral", "create:depot_0"),
    event = ask("Event name", "pass"),
    releaseEnabled = releaseEnabled,
    releaseSide = ask("Release redstone side", "back"),
    releasePulse = tonumber(ask("Release pulse seconds", "0.15")) or 0.15,
    poll = tonumber(ask("Poll interval seconds", "0.10")) or 0.10,
    debounceSeconds = tonumber(ask("Debounce seconds", "2.0")) or 2.0,
    heartbeatSeconds = tonumber(ask("Heartbeat seconds", "60")) or 60,
    verboseDebounce = askBool("Print debounce skips?", false),
  }

  patchConfigNode(fs.combine(pkg.targetDir, "config.lua"), values)
end

local function choosePackage()
  header("Project ESP - CC installer " .. INSTALLER_VERSION)
  print("1) Master - factory order fulfillment")
  print("2) Node   - package checkpoint sensor")
  print("3) Exit")

  while true do
    local choice = ask("Choose install target", "1")
    local lowered = choice:lower()

    if choice == "1" or lowered == "master" then return PACKAGES.master end
    if choice == "2" or lowered == "node" then return PACKAGES.node end
    if choice == "3" or lowered == "exit" then return nil end

    print("Invalid choice. Try again, state-of-the-art human interface.")
  end
end

local function main()
  term.clear()
  term.setCursorPos(1, 1)

  ensureHttp()

  local pkg = choosePackage()
  if not pkg then
    print("Installer cancelled.")
    return
  end

  header("Install " .. pkg.label)
  print("Target folder: " .. pkg.targetDir)

  if fs.exists(pkg.targetDir) then
    local overwrite = askBool("Target already exists. Overwrite files?", true)
    if not overwrite then
      print("Cancelled. Cowardly but understandable.")
      return
    end
  end

  downloadPackage(pkg)

  if pkg == PACKAGES.master then
    configureMaster(pkg)
  elseif pkg == PACKAGES.node then
    configureNode(pkg)
  end

  createRunner(pkg)

  header("Done")
  print("Installed: " .. pkg.label)
  print("Files installed into:")
  print("  " .. pkg.targetDir)
  print("")
  print("Run with:")
  print("  " .. pkg.runnerName)
  print("")
  if pkg == PACKAGES.node then
    print("Note:")
    print("  ESP assigns nodeId on first start.")
    print("  " .. fs.combine(pkg.targetDir, "node_state.json") .. " is local runtime state, do not commit it.")
  end
end

local ok, err = pcall(main)
if not ok then
  print("")
  print("[INSTALL FAILED]")
  print(err)
end
