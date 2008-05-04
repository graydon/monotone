
mtn_setup()

-- We don't check options with paths in them; on Windows, sometimes
-- the paths have '\' for directory separators, sometimes '/', and we
-- don't have a way to predict which or fix them.
check(mtn("automate", "get_option", "branch"), 0, true, false)
canonicalize("stdout")
check("testbranch\n" == readfile("stdout"))

check(mtn("automate", "get_option", "key"), 0, true, false)
canonicalize("stdout")
check("tester@test.net\n" == readfile("stdout"))

-- Ensure that 'get_options' gets the workspace options even when run via stdio
check(mtn_ws_opts("automate", "stdio"), 0, true, false, "l10:get_option6:branche")
canonicalize("stdout")
check("0:0:l:11:testbranch\n" == readfile("stdout"))

-- end of file
