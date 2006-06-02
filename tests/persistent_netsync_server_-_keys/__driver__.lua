
mtn_setup()
netsync_setup()

writefile("testfile", "I am you and you are me and we are all together.")
check(cmd(mtn2("add", "testfile")), 0, false, false)
check(cmd(mtn2("commit", "--branch=testbranch", "--message=foo")), 0, false, false)

check(cmd(mtn2("genkey", "foo@foo")), 0, false, false, string.rep("foo@foo\n",2))

srv = netsync_serve_start("testbranch")

netsync_client_run("push", "testbranch", 2)
netsync_client_run("pull", "testbranch", 3)

check(cmd(mtn3("ls", "keys")), 0, true, false)
check(not qgrep("foo@foo", "stdout"))

writefile("testfile", "stuffty stuffty")
check(cmd(mtn2("commit", "--branch=testbranch", "--message=foo", "--key=foo@foo")), 0, false, false)

netsync_client_run("push", "testbranch", 2)
netsync_client_run("pull", "testbranch", 3)

srv:finish()

check(cmd(mtn3("ls", "keys")), 0, true, false)
check(qgrep("foo@foo", "stdout"))
