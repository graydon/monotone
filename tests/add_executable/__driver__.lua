
skip_if(ostype == "Windows")
skip_if(not existsonpath("chmod"))
skip_if(not existsonpath("test"))
mtn_setup()


writefile("foo", "blah blah")
check({"chmod", "755", "foo"})
-- Have to use RAW_MTN, because we're testing the standard hooks...
check(raw_mtn("--rcfile=test_hooks.lua", "add", "foo"), 0, false, false)
commit()

-- Have to use RAW_MTN, because we're testing the standard hooks...
check(raw_mtn("--rcfile=test_hooks.lua", "checkout", "--branch=testbranch", "codir"), 0, false, false)
check({"test", "-x", "codir/foo"})
