function ignore_file(name)
   -- project specific
   io.stderr:write("considering ignoring " .. name .. "\n")
   if (ignored_files == nil) then
      ignored_files = {}
      local ignfile = io.open(".mtn-ignore", "r")
      if (ignfile ~= nil) then
         for l in ignfile:lines() do table.insert(ignored_files, l) end
         ignfile:close()
      end
   end
   local decision = false
   for i, line in pairs(ignored_files)
   do
      if (line ~= nil) then
         local pcallstatus, result = pcall(function() return regex.search(line, name) end)
         if pcallstatus == true then
            -- no error from the regex.search call
            if result == true then
               io.stderr:write("- ignoring " .. name 
                               .. " due to " .. i .. ", /" .. line .. "/\n")
	            decision = true
	            break
	         end
         else
            -- regex.search had a problem, warn the user their .mtn-ignore file syntax is wrong
            io.stderr:write("WARNING: line " .. i 
                       .. " in your .mtn-ignore file caused error '"
                       .. result .. "' while matching filename '" .. name 
                       .. "'.\nignoring this regex for all remaining files.\n")
            ignored_files[i] = nil
         end
      end
   end
   if decision == false then
      io.stderr:write("- not ignoring " .. name .. "\n")
   end
   return decision
end
