function ignore_file(name)
if (string.find(name, "test_hooks.lua")) then return true end
if (string.find(name, "test.db")) then return true end
if (string.find(name, "%.ignore$")) then return true end
return false
end
