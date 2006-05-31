
mtn_setup()
netsync_setup()

writefile("testfile", "version 0 of test file")
check(cmd(mtn("add", "testfile")), 0, false, false)
commit("testbranch")
f_ver = sha1("testfile")
ver = base_revision()

run_netsync("pull", "testbranch")

check(cmd(mtn2("ls", "certs", ver)), 0, true)
rename("stdout", "certs")
check(qgrep("date", "certs"))
check(qgrep("author", "certs"))
check(qgrep("branch", "certs"))
check(qgrep("changelog", "certs"))
check(not qgrep("bad", "certs"))

check(cmd(mtn2("automate", "get_revision", ver)), 0, true)
canonicalize("stdout")
check(sha1("stdout") == ver)

check(cmd(mtn2("automate", "get_file", f_ver)), 0, true)
canonicalize("stdout")
check(sha1("stdout") == f_ver)

check(cmd(mtn("db", "info")), 0, true)
info1 = sha1("stdout")
check(cmd(mtn2("db", "info")), 0, true)
info2 = sha1("stdout")
check(info1 == info2)
