function ignore_file(name)
if (string.find(name, "%~$")) then return true end
return false
end
