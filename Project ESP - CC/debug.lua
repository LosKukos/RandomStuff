local ok, a, b, c, d = pcall(function()
  return ae.craftItem({ name = "minecraft:iron_ingot", count = 64 })
end)

print("ok:", ok)
print("a:", type(a), type(a) == "table" and textutils.serialise(a) or tostring(a))
print("b:", type(b), type(b) == "table" and textutils.serialise(b) or tostring(b))
print("c:", type(c), type(c) == "table" and textutils.serialise(c) or tostring(c))
print("d:", type(d), type(d) == "table" and textutils.serialise(d) or tostring(d))
