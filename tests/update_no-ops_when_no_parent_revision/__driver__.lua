
mtn_setup()

addfile("foo", "blah balh")

check(mtn("update", "--branch=testbranch"), 1, false, false)
