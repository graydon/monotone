
mtn_setup()

addfile("foo", "bar")
commit()
key = "reviewer@test.net"
check(mtn("genkey", key), 0, false, false, string.rep(key.."\n", 2))
check(mtn("approve", "-k", key, base_revision()), 0, false, false)
check(mtn("ls", "certs", base_revision()), 0, true, false)
check(qgrep(key, "stdout"))
