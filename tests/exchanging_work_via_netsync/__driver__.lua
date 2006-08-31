
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

writefile("testfile", "version 0 of test file")
check(mtn("add", "testfile"), 0, false, false)
commit()
f_ver = {}
ver = {}
f_ver[0] = sha1("testfile")
ver[0] = base_revision()

writefile("testfile", "version 1 of test file")
commit()
f_ver[1] = sha1("testfile")
ver[1] = base_revision()

netsync.pull("testbranch")

check(mtn2("ls", "certs", ver[0]), 0, true)
rename("stdout", "certs")
check(qgrep("date", "certs"))
check(qgrep("author", "certs"))
check(qgrep("branch", "certs"))
check(qgrep("changelog", "certs"))
check(not qgrep("bad", "certs"))

check(mtn2("ls", "certs", ver[1]), 0, true)
rename("stdout", "certs")
check(qgrep("date", "certs"))
check(qgrep("author", "certs"))
check(qgrep("branch", "certs"))
check(qgrep("changelog", "certs"))
check(not qgrep("bad", "certs"))

for _, what in pairs({{cmd = "get_revision", ver = ver[0]},
                      {cmd = "get_revision", ver = ver[1]},
                      {cmd = "get_file", ver = f_ver[0]},
                      {cmd = "get_file", ver = f_ver[1]}}) do
  check(mtn2("automate", what.cmd, what.ver), 0, true)
  canonicalize("stdout")
  check(sha1("stdout") == what.ver)
end

check(mtn("db", "info"), 0, true)
canonicalize("stdout")
info1 = sha1("stdout")
check(mtn2("db", "info"), 0, true)
canonicalize("stdout")
info2 = sha1("stdout")
check(info1 == info2)

