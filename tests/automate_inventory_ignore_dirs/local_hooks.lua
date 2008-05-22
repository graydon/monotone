function ignore_file(name)
   local result = false;
   io.stderr:write("ignore_file: '" .. name .. "':")

   if (name == "source/ignored_dir") then result = true end
   if (name == "source/ignored_dir/oops") then result = true end
   if (name == "source/ignored_1") then result = true end

   if result then
      io.stderr:write(" true\n")
   else
      io.stderr:write(" false\n")
   end
   return result
end
