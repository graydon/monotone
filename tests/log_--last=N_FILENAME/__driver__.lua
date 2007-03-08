
mtn_setup()

addfile("foo", "foo")
commit("testbranch", "Addition of foo.")

addfile("bar", "bar")
commit("testbranch", "Addition of bar.")

addfile("baz", "baz")
commit("testbranch", "Addition of baz.")

check(mtn("log", "--last=1", "foo"), 0, true, false)
check(grep("^[\\| ]+Revision:", "stdout"), 0, true, false)
check(numlines("stdout") == 1)
