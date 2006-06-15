
mtn_setup()

writefile("foo.testbranch", "this is the testbranch version")
writefile("foo.otherbranch", "this version goes in otherbranch")

copy("foo.testbranch", "foo")
check(mtn("add", "foo"), 0, false, false)
commit()

copy("foo.otherbranch", "foo")
commit("otherbranch")

check(mtn("--branch=testbranch", "checkout"), 0, false, false)
check(samefile("testbranch/foo", "foo.testbranch"))
