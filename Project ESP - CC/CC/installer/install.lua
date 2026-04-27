-- installer/install.lua
-- CC installer for Project ESP - CC
-- Installs either master or node package from GitHub into the current ComputerCraft computer.

local INSTALLER_VERSION = "1.0.0"

local BASE_RAW = "https://raw.githubusercontent.com/LosKukos/RandomStuff/main/Project%20ESP%20-%20CC/CC"

local PACKAGES = {
  master = {
    label = "Master - factory order fulfillment",
    sourceDir = "master",
    targetDir = "master",
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
    targetDir = "node",
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

local function pause()
  print("")
  write("Press ENTER to continue...")
  read()
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
  return body
end

local function ensureDir(path)
  if fs.exists(path) and not fs.isDir(path) then
    error("Target exists but is not a directory: " .. path)
  end
  if not fs.exists(path) then
    fs.makeDir(path)
  end
end

local function writeFile(path, content)
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
    content = content:gsub(key .. "%s*=%s*" .. string.format("%q", "") .. ",", key .. " = " .. string.format("%q", value) .. ",")
    content = content:gsub(key .. "%s*=%s*" .. '"[^"]*"' .. ",", key .. " = " .. string.format("%q", value) .. ",")
  end

  writeFile(path, content)
end

local function patchConfigNode(path, values)
  local content = readFile(path)
  if not content then error("Missing config: " .. path) end

  local stringReplacements = {
    nodeId = values.nodeId,
    nodeName = values.nodeName,
    scanner = values.scanner,
    releaseSide = values.releaseSide,
    espBase = values.espBase,
    event = values.event,
  }

  for key, value in pairs(stringReplacements) do
    content = content:gsub(key .. "%s*=%s*" .. '"[^"]*"' .. ",", key .. " = " .. string.format("%q", value) .. ",")
  end

  content = content:gsub("releaseEnabled%s*=%s*[%a]+,", "releaseEnabled = " .. tostring(values.releaseEnabled) .. ",")
  content = content:gsub("releasePulse%s*=%s*[%d%.]+,", "releasePulse = " .. tostring(values.releasePulse) .. ",")
  content = content:gsub("poll%s*=%s*[%d%.]+,", "poll = " .. tostring(values.poll) .. ",")
  content = content:gsub("debounceSeconds%s*=%s*[%d%.]+,", "debounceSeconds = " .. tostring(values.debounceSeconds) .. ",")
  content = content:gsub("verboseDebounce%s*=%s*[%a]+,", "verboseDebounce = " .. tostring(values.verboseDebounce) .. ",")

  writeFile(path, content)
end

local function createRunner(pkg)
  local runnerName = pkg.targetDir
  local script = table.concat({
    "shell.run(" .. string.format("%q", fs.combine(pkg.targetDir, pkg.entry)) .. ")"
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
    nodeId = ask("Node ID", "node_1"),
    nodeName = ask("Node display name", "Node 1"),
    scanner = ask("Scanner/depot peripheral", "create:depot_0"),
    event = ask("Event name", "pass"),
    releaseEnabled = releaseEnabled,
    releaseSide = ask("Release redstone side", "back"),
    releasePulse = tonumber(ask("Release pulse seconds", "0.15")) or 0.15,
    poll = tonumber(ask("Poll interval seconds", "0.10")) or 0.10,
    debounceSeconds = tonumber(ask("Debounce seconds", "2.0")) or 2.0,
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
    if choice == "1" or choice:lower() == "master" then return PACKAGES.master end
    if choice == "2" or choice:lower() == "node" then return PACKAGES.node end
    if choice == "3" or choice:lower() == "exit" then return nil end
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
  print("Run with:")
  print("  " .. pkg.targetDir)
  print("")
  print("Or manually:")
  print("  cd " .. pkg.targetDir)
  print("  " .. pkg.entry:gsub("%.lua$", ""))
end

local ok, err = pcall(main)
if not ok then
  print("")
  print("[INSTALL FAILED]")
  print(err)
end
