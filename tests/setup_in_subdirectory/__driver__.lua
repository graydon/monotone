
mtn_setup()

-- Set up a project
check(mtn("setup", "--branch=testbranch", "foo"), 0, false, false)

-- Then set up another project in a subdirectory without giving a database
-- or branch.  mtn SHOULD fail on this.
check(indir("foo", safe_mtn("setup", "bar")), 1, false, false)
