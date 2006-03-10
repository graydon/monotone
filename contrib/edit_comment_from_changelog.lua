std_edit_comment = edit_comment
function edit_comment(basetext, user_log_message)
	local tmp, tname = temp_file()
	if (tmp == nil) then return nil end
	if (user_log_message == "") then
		local ChangeLog = io.open("ChangeLog", "r")
		if ChangeLog == nil then
			return std_edit_comment(basetext, user_log_message)
		end
		local line = ChangeLog:read()
		local msg = ""
		local n = 0
		while(line ~= nil and n < 2) do
			if (string.find(line, "^[^%s]")) then
				n = n + 1
			end
			if (n < 2 and not string.find(line, "^%s*$"))
			then
				msg = msg .. line .. "\n"
			end
			line = ChangeLog:read()
		end
		user_log_message = msg
		io.close(ChangeLog)
	end
	return std_edit_comment(basetext, user_log_message)
end
