function ignore_file(name)
   if (string.find(name, "%.o$")) then return true end
   return false;
end
