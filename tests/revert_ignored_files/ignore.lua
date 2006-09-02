function ignore_file(name)
   if (string.find(name, "%.ignored$")) then return true end
   return false
end
