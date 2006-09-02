
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

writefile("testfile", "version 0 of test file")
check(mtn("add", "testfile"), 0, false, false)
commit("testbranch")
f_ver = sha1("testfile")
ver = base_revision()

netsync.pull("testbranch")

check(mtn2("ls", "certs", ver), 0, true)
rename("stdout", "certs")
check(qgrep("date", "certs"))
check(qgrep("author", "certs"))
check(qgrep("branch", "certs"))
check(qgrep("changelog", "certs"))
check(not qgrep("bad", "certs"))

check(mtn2("automate", "get_revision", ver), 0, true)
canonicalize("stdout")
check(sha1("stdout") == ver)

check(mtn2("automate", "get_file", f_ver), 0, true)
canonicalize("stdout")
check(sha1("stdout") == f_ver)

check(mtn("db", "info"), 0, true)
info1 = sha1("stdout")
check(mtn2("db", "info"), 0, true)
info2 = sha1("stdout")
check(info1 == info2)
