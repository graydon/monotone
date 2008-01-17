function ignore_file(name)
   io.stderr:write("ignore_file: '" .. name .. "':\n")

   if (name == "source/ignored_dir") then return true end
   if (name == "source/ignored_1") then return true end
   return false
end
