function glob_to_pattern(glob)
	local pattern
	
	-- escape all special characters:
	pattern = string.gsub(glob, "([%^%$%(%)%%%.%[%]%*%+%-%?])", "%%%1")

	-- convert the glob's ones to pattern's:
	pattern = string.gsub(pattern, "%%%*", "[^/]*")
	pattern = string.gsub(pattern, "%%%?", ".")

	return pattern
end

function ignore_file(name)
	local dir, pat1, pat2

	dir = string.gsub(name, "/[^/]+$", "/")
	if (dir == name) then dir = "" end
	pat1 = "^" .. glob_to_pattern(dir)
	
	for line in io.lines(dir .. ".cvsignore") do
		pat2 = glob_to_pattern(line) .. "$"
		if (string.find(name, pat1 .. pat2)) then return true end
	end

	return false
end
