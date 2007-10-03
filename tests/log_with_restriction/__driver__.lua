-- This test checks that 'mtn log' of a file only shows 
-- only revisions containing that file.

mtn_setup()

addfile("early", "early")
commit("testbranch", "Addition of an early file.")

addfile("foo", "foo")
addfile("bar", "bar")
commit("testbranch", "Addition of foo and bar.")
rev_foo1 = base_revision()

writefile("bar", "changed bar")
commit("testbranch", "bar has changed.")

writefile("foo", "changed foo once")
commit("testbranch", "foo has changed once.")
rev_foo2 = base_revision()

check(mtn("drop", "--bookkeep-only", "bar"), 0, false, false)
commit("testbranch", "Dropped bar.")

writefile("foo", "changed foo again")
commit("testbranch", "foo has changed again.")
rev_foo3 = base_revision()

check(mtn("log", "foo"), 0, true, false)
rename("stdout", "log")

check(grep("^[\\|\\\\\/ ]+Revision:", "log"), 0, true, false)
rename("stdout", "revs")
check(numlines("revs") == 3)

check(grep("^[\\|\\\\\/ ]+Revision: " .. rev_foo1, "log"), 0, true)
check(grep("^[\\|\\\\\/ ]+Revision: " .. rev_foo2, "log"), 0, true)
check(grep("^[\\|\\\\\/ ]+Revision: " .. rev_foo3, "log"), 0, true)
