function ignore_file(name)
   if (string.find(name, "1$")) then return true end
   if (string.find(name, "2$")) then return true end
   return false
end
