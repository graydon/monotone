
mtn_setup()

writefile("foo", "blah blah")
check(mtn("setup", "--branch=testbranch", "foo"), 1, false, false)
mkdir("bar")
check(mtn("setup", "--branch=testbranch", "bar"), 0, false, false)
check(exists("bar/_MTN"))
remove("bar/_MTN")
check(indir("bar", mtn("setup", "--branch=testbranch")), 0, false, false)
