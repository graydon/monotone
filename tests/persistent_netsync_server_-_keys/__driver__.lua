
mtn_setup()
netsync.setup()

writefile("testfile", "I am you and you are me and we are all together.")
check(cmd(mtn2("add", "testfile")), 0, false, false)
check(cmd(mtn2("commit", "--branch=testbranch", "--message=foo")), 0, false, false)

check(cmd(mtn2("genkey", "foo@foo")), 0, false, false, string.rep("foo@foo\n",2))

srv = netsync.start("testbranch")

srv:push("testbranch", 2)
srv:pull("testbranch", 3)

check(cmd(mtn3("ls", "keys")), 0, true, false)
check(not qgrep("foo@foo", "stdout"))

writefile("testfile", "stuffty stuffty")
check(cmd(mtn2("commit", "--branch=testbranch", "--message=foo", "--key=foo@foo")), 0, false, false)

srv:push("testbranch", 2)
srv:pull("testbranch", 3)

srv:finish()

check(cmd(mtn3("ls", "keys")), 0, true, false)
check(qgrep("foo@foo", "stdout"))
