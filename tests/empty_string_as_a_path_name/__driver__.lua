-- Most commands can't handle "" as a parameter, but some can.
mtn_setup()

addfile("testfile", "blah blah")
commit()
rev = base_revision()

mkdir("foo")
check(indir("foo", mtn("setup", "--branch=testbranch", "")), 1, false, false)
check(indir("foo", mtn("checkout", "--revision", rev, "")), 1, false, false)
check(indir("foo", mtn("checkout", "--branch=testbranch", "")), 1, false, false)

check(mtn("--bookkeep-only", "add", ""), 1, false, false)
check(mtn("--bookkeep-only", "drop", ""), 1, false, false)
check(mtn("--bookkeep-only", "rename", "testfile", ""), 1, false, false)
check(mtn("--bookkeep-only", "rename", "", "otherfile"), 1, false, false)

-- These two have the same meaning, and the same result.
check(mtn("revert", "."), 0, false, false)
check(mtn("revert", ""), 0, false, false)
