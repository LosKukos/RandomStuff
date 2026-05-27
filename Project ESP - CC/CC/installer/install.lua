-- installer/install.lua
-- CC installer for Project ESP - CC
-- v1.1.0

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
    files = { "config.lua", "util.lua", "esp.lua", "me.lua", "factory.lua", "orders.lua", "players.lua", "master.lua" }
  },
  node = {
    label = "Node - dumb package checkpoint",
    sourceDir = "node",
    targetDir = fs.combine(INSTALL_ROOT, "node"),
    runnerName = "node",
    entry = "node.lua",
    files = { "config.lua", "util.lua", "esp.lua", "scanner.lua", "node.lua", "README.md" }
  }
}

local function ask(prompt, default)
  write(prompt)
  if default ~= nil and default ~= "" then write(" [" .. tostring(default) .. "]") end
  write(": ")
  local value = tostring(read() or ""):match("^%s*(.-)%s*$")
  if value == "" and default ~= nil then return default end
  return value
end

local function askBool(prompt, default)
  local suffix = default and "Y/n" or "y/N"
  while true do
    write(prompt .. " (" .. suffix .. "): ")
    local v = tostring(read() or ""):lower():match("^%s*(.-)%s*$")
    if v == "" then return default end
    if v == "y" or v == "yes" then return true end
    if v == "n" or v == "no" then return false end
    print("Please answer y/n.")
  end
end

local function download(url)
  local res, err = http.get(url)
  if not res then return nil, err end
  local body = res.readAll(); res.close()
  if not body or body == "" then return nil, "empty_response" end
  return body
end

local function writeFile(path, content)
  local dir = fs.getDir(path)
  if dir and dir ~= "" and not fs.exists(dir) then fs.makeDir(dir) end
  local f = fs.open(path, "w")
  if not f then error("Could not write: " .. path) end
  f.write(content); f.close()
end

local function buildUrl(pkg, fileName)
  return BASE_RAW .. "/" .. pkg.sourceDir:gsub(" ", "%%20") .. "/" .. fileName:gsub(" ", "%%20")
end

local function downloadPackage(pkg)
  if not fs.exists(pkg.targetDir) then fs.makeDir(pkg.targetDir) end
  for _, fileName in ipairs(pkg.files) do
    local url = buildUrl(pkg, fileName)
    local target = fs.combine(pkg.targetDir, fileName)
    print("[GET] " .. pkg.sourceDir .. "/" .. fileName)
    local body, err = download(url)
    if not body then error("Failed: " .. fileName .. ": " .. tostring(err)) end
    writeFile(target, body)
  end
end

local function patchStringField(content, key, value)
  local replacement = key .. " = " .. string.format("%q", tostring(value)) .. ","
  local updated, count = content:gsub(key .. "%s*=%s*\"[^\"]*\"%s*,", replacement)
  if count == 0 then updated = content:gsub(key .. "%s*=%s*'[^']*'%s*,", replacement) end
  return updated
end

local function configureMaster(pkg)
  print("=== Master config ===")
  local configPath = fs.combine(pkg.targetDir, "config.lua")
  local f = fs.open(configPath, "r"); if not f then error("Missing config") end
  local content = f.readAll(); f.close()
  local values = {
    espBase = ask("ESP base URL", "http://10.0.1.17"),
    meBridge = ask("ME bridge peripheral", "me_bridge_0"),
    packager = ask("Create packager peripheral", "Create_Packager_0"),
    bufferChest = ask("Buffer chest peripheral", "minecraft:barrel_0"),
    depot = ask("Depot peripheral", "create:depot_2"),
    bridgeChestDirection = ask("ME export direction", "right"),
    armSetRedstoneSide = ask("Release redstone side", "back"),
  }
  for key, value in pairs(values) do content = patchStringField(content, key, value) end
  writeFile(configPath, content)
end

local function configureNode(pkg)
  print("=== Node config ===")
  local configPath = fs.combine(pkg.targetDir, "config.lua")
  local f = fs.open(configPath, "r"); if not f then error("Missing config") end
  local content = f.readAll(); f.close()
  local values = {
    espBase = ask("ESP base URL", "http://10.0.1.17"),
    nodeName = ask("Node display name", "Node 1"),
    scanner = ask("Scanner peripheral", "create:depot_0"),
    releaseSide = ask("Release redstone side", "back"),
  }
  for key, value in pairs(values) do content = patchStringField(content, key, value) end
  writeFile(configPath, content)
end

local function createRunner(pkg)
  local script = table.concat({
    "local oldDir = shell.dir()",
    "shell.setDir(" .. string.format("%q", pkg.targetDir) .. ")",
    "local ok, err = pcall(function() shell.run(" .. string.format("%q", pkg.entry) .. ") end)",
    "shell.setDir(oldDir)",
    "if not ok then error(err, 0) end",
  }, "\n") .. "\n"
  writeFile(pkg.runnerName, script)
  print("[OK] Runner: " .. pkg.runnerName)
end

local function main()
  term.clear(); term.setCursorPos(1, 1)
  if not http or not http.get then error("HTTP API disabled.") end
  print("Project ESP - CC installer " .. INSTALLER_VERSION)
  print("1) Master  2) Node  3) Exit")
  local choice = ask("Choose", "1")
  local pkg = nil
  if choice == "1" or choice:lower() == "master" then pkg = PACKAGES.master
  elseif choice == "2" or choice:lower() == "node" then pkg = PACKAGES.node
  else print("Cancelled."); return end
  print("Installing: " .. pkg.label)
  if fs.exists(pkg.targetDir) then
    if not askBool("Target exists. Overwrite?", true) then print("Cancelled."); return end
  end
  downloadPackage(pkg)
  if pkg == PACKAGES.master then configureMaster(pkg)
  else configureNode(pkg) end
  createRunner(pkg)
  print("=== Done ===")
  print("Run with: " .. pkg.runnerName)
end

local ok, err = pcall(main)
if not ok then print("[INSTALL FAILED]"); print(err) end
