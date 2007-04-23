
mtn_setup()

-- The 'version' command behaves exactly as the '--version' option.
check(mtn("version"), 0, true, 0)
rename("stdout", "out")
check(mtn("--version"), 0, true, 0)
check(samefile("stdout", "out"))

-- The 'version' command prints no detailed information.
check(mtn("version"), 0, true, 0)
output = readfile("stdout")
check(string.find(output, "Running on") == nil)

-- The 'version' command allows a '--full' option.
check(mtn("version", "--full"), 0, true, 0)
output = readfile("stdout")
check(string.find(output, "Running on") ~= nil)

-- The '--version' option does not allow a '--full' option (because the
-- latter is not global).
check(mtn("--version", "--full"), 0, true, 0)
output = readfile("stdout")
check(string.find(output, "Running on") == nil)
